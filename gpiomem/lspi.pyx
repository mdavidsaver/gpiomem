
import os
import logging

from libc.errno cimport errno
from libc.stdint cimport uint32_t

_log = logging.getLogger(__name__)

cdef extern from "fcntl.h":
    cdef int O_CLOEXEC

cdef extern from "<sys/ioctl.h>":
    cdef int ioctl(int fd, unsigned long req, ...)

cdef extern from "<linux/spi/spidev.h>":
    cdef int SPI_IOC_WR_MODE32
    cdef int SPI_IOC_MESSAGE(int n)

    struct spi_ioc_transfer:
        unsigned long tx_buf, rx_buf
        unsigned len, speed_hz
        unsigned short delay_usecs
        unsigned char bits_per_word, cs_change, tx_nbits, rx_nbits

cdef class _lspi(object):
    cdef unsigned mode, speed, delay
    cdef int fd

    def __init__(self, bus=0, device=0, name=None):
        if name is None:
            name = '/dev/spidev%u.%u'%(bus, device)

        self.fd = <int?>os.open(name, os.O_RDWR|O_CLOEXEC)

        self.mode = self.speed = self.delay = 0

    def __dealloc__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, A,B,C):
        self.close()

    def close(self):
        if self.fd != -1:
            os.close(self.fd)
            self.fd = -1

    def xfer(self, bytes data):
        cdef char* datap = data
        cdef char* retp
        cdef spi_ioc_transfer X
        cdef uint32_t mode = self.mode

        if ioctl(self.fd, SPI_IOC_WR_MODE32, &mode)==-1:
            raise OSError(errno, "SPI Mode")

        ret = bytearray(len(data))
        retp = ret

        X.tx_buf = <unsigned long>datap
        X.rx_buf = <unsigned long>retp
        X.len = len(ret)
        X.bits_per_word = 8
        X.speed_hz = self.speed
        X.delay_usecs = self.delay

        if ioctl(self.fd, SPI_IOC_MESSAGE(1), X)==-1:
            raise OSError(errno, "SPI Transfer")

        return ret
