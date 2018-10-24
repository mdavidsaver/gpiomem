import logging

from libc.errno cimport errno

from posix.fcntl cimport O_RDWR, O_SYNC, open
from posix.unistd cimport close

from posix.mman cimport mmap, munmap, off_t, PROT_READ, PROT_WRITE, MAP_SHARED

_log = logging.getLogger(__name__)

cdef extern from "<string.h>":
    void memset(void *dest, int c, size_t s)

cdef extern from "gnummio.h":
    ctypedef int uint8_t
    ctypedef int uint16_t
    ctypedef int uint32_t
    ctypedef int uint64_t

    uint8_t ioread8(void *ptr)
    uint16_t ioread16(void *ptr)
    uint32_t ioread32(void *ptr)
    void iowrite8(void *ptr, uint8_t v)
    void iowrite16(void *ptr, uint16_t v)
    void iowrite32(void *ptr, uint32_t v)

    void* ptr_add(void* base, size_t offset)

from posix.ioctl cimport ioctl

cdef extern from "<linux/spi/spidev.h>":
    enum: SPI_IOC_WR_MODE32
    int SPI_IOC_MESSAGE(int n) # actually a function type macro

    struct spi_ioc_transfer:
        uint64_t tx_buf, rx_buf
        uint32_t len, speed_hz
        uint16_t delay_usecs
        uint8_t bits_per_word, cs_change, tx_nbits, rx_nbits

cdef extern from *:
    enum: O_CLOEXEC

cdef class MMIO(object):
    """MMIO(name, length, offset=0)

    MMIO Access.  Specifically for the BCM2835 (aka. raspberry pi 3)
    """
    cdef int fd
    cdef size_t length
    cdef void* base

    cdef readonly unsigned npins
    cdef public bint debug

    def __cinit__(self, *args, **kws):
        self.npins = 54
        self.debug = False

    def __init__(self, name, size_t length, size_t offset=0):
        self.fd = open(name, O_RDWR|O_CLOEXEC|O_SYNC)
        self.length = length

        self.base = mmap(NULL, self.length, PROT_READ|PROT_WRITE, MAP_SHARED, self.fd, offset)
        if self.base==<void*>-1:
            raise OSError(errno, "Failed to mmap()")

    def __dealloc__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, A,B,C):
        self.close()

    def close(self):
        if self.fd != -1:
            close(self.fd)
            munmap(self.base, self.length)
            self.fd = -1

    cdef uint32_t read32(self, size_t offset):
        cdef uint32_t val = ioread32(ptr_add(self.base, offset))
        if self.debug:
            _log.debug("read32(0x%x) -> 0x%08x", offset, val)
        return val

    cdef void write32(self, size_t offset, uint32_t val):
        if self.debug:
            _log.debug("write32(0x%x, 0x%08x)", offset, val)
        iowrite32(ptr_add(self.base, offset), val)

    cpdef unsigned getalt(self, unsigned pin):
        "Return alternate function mode for a pin"
        cdef size_t offset, bit, cur

        if pin>=54:
            raise ValueError("pin out of range [0,54)")

        offset = 4*(pin/10)
        bit = 3*(pin%10)

        cur = self.read32(offset)

        return (cur>>bit)&7

    def setalt(self, unsigned pin, unsigned mode):
        """Set pin mode
        """
        cdef size_t offset, bit, cur

        if mode>7:
            raise ValueError("mode out of range [0, 7]")

        if self.getalt(pin)==mode:
            return

        offset = 4*(pin/10)
        bit = 3*(pin%10)

        cur = self.read32(offset)

        cur &= ~(7<<bit)
        cur |= mode<<bit

        self.write32(offset, cur)

    cpdef void output(self, unsigned pin, bint val):
        """output(pin, val)

        Set output pin
        """
        cdef size_t offset, bit, mask

        if pin>=54:
            raise ValueError("pin out of range [0,54)")
        offset = pin/32
        bit = pin%32
        mask = 1<<bit

        if val:
            self.write32(0x1c + 4*offset, mask)
        else:
            self.write32(0x28 + 4*offset, mask)

    cpdef bint input(self, unsigned pin):
        """input(pin) -> bool

        read input pin
        """
        cdef size_t offset, bit, mask, cur

        if pin>=54:
            raise ValueError("pin out of range [0,54)")
        offset = pin/32
        bit = pin%32
        mask = 1<<bit

        cur = self.read32(0x34 + 4*offset)

        return cur&mask

    def spi3(self, bytes inp, unsigned sclk, unsigned mosi, unsigned miso, bint cpol=1, bint cpha=1):
        """Bit-bang SPI in mode=3 (cpol=1, cpha=1)
        """
        cdef size_t mask, n
        cdef unsigned b, mosib = self.npins, misob = self.npins
        cdef char* inpb = inp
        cdef char* retv = NULL

        ret = bytearray(len(inp)) # initially zeros
        retv = ret

        # assume initially SCLK==cpol

        for n in range(len(inp)):
            for b in range(8):
                mask = 1<<(7-b)

                self.output(sclk, not cpol)

                if cpha:
                    self.output(mosi, inpb[n]&mask) # cpha=1 setup
                elif self.input(miso):              # cpha=0 sample
                    retv[n]|=mask

                self.output(sclk, cpol)

                if not cpha:
                    self.output(mosi, inpb[n]&mask) # cpha=0 setup
                elif self.input(miso):              # cpha=1 sample
                    retv[n]|=mask

        return ret

cdef class lspi(object):
    """Linux SPIDEV interface
    """
    cdef public unsigned mode, speed, delay
    cdef int fd

    def __cinit__(self, *args, **kws):
        self.mode = self.speed = self.delay = 0

    def __init__(self, bus=0, device=0, name=None):
        if name is None:
            name = '/dev/spidev%u.%u'%(bus, device)

        self.fd = open(name, O_RDWR|O_CLOEXEC)

    def __dealloc__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, A,B,C):
        self.close()

    def close(self):
        if self.fd != -1:
            close(self.fd)
            self.fd = -1

    def xfer(self, bytes data):
        cdef char* datap = data
        cdef char* retp
        cdef spi_ioc_transfer X[1]
        cdef uint32_t mode = self.mode

        memset(<void*>X, 0, sizeof(X))

        if ioctl(self.fd, SPI_IOC_WR_MODE32, &mode)==-1:
            raise OSError(errno, "SPI Mode")

        ret = bytearray(len(data))
        retp = ret

        X[0].tx_buf = <size_t>datap
        X[0].rx_buf = <size_t>retp
        X[0].len = len(ret)
        X[0].bits_per_word = 8
        X[0].speed_hz = self.speed
        X[0].delay_usecs = self.delay

        if ioctl(self.fd, SPI_IOC_MESSAGE(1), X)==-1:
            raise OSError(errno, "SPI Transfer")

        return ret
