
import os

#from libc.stdint import uint32_t, uint16_t, uint8_t
from libc.errno cimport errno

cdef extern from "fcntl.h":
    cdef int O_CLOEXEC

cdef extern from "sys/mman.h":
    cdef int PROT_READ
    cdef int PROT_WRITE
    cdef int MAP_SHARED
    cdef void* MAP_FAILED

    ctypedef int off_t;

    # definitions modified to accept char*
    void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
    int munmap(void *addr, size_t length)

cdef extern from "gnummio.h":
    ctypedef int uint8_t
    ctypedef int uint16_t
    ctypedef int uint32_t

    uint8_t ioread8(void *ptr)
    uint16_t ioread16(void *ptr)
    uint32_t ioread32(void *ptr)
    void iowrite8(void *ptr, uint8_t v)
    void iowrite16(void *ptr, uint16_t v)
    void iowrite32(void *ptr, uint32_t v)

    void* ptr_add(void* base, size_t offset)

cdef class MMIO(object):
    cdef int fd
    cdef size_t length
    cdef void* base

    cdef public unsigned width

    def __init__(self, name, size_t length, size_t offset=0):
        self.width = 32
        self.fd = <int?>os.open(name, os.O_RDWR|O_CLOEXEC|os.O_SYNC)
        self.length = length

        self.base = mmap(NULL, self.length, PROT_READ|PROT_WRITE, MAP_SHARED, self.fd, offset)
        if self.base==MAP_FAILED:
            raise OSError(errno, "Failed to mmap()")

    def __dealloc__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, A,B,C):
        self.close()

    def close(self):
        if self.fd != -1:
            os.close(self.fd)
            munmap(self.base, self.length)
            self.fd = -1

    cdef void _check(self, unsigned width, size_t offset):
        if width not in (8, 16, 32):
            raise ValueError("Unsupported width %d"%width)
        width /= 8
        if offset%width!=0:
            raise ValueError("Unaligned access")

        if offset >= self.length or width > self.length-offset:
            raise ValueError("Out of range")

    def io(self, prog):
        cdef size_t offset, mask, fullmask, value, V
        cdef unsigned width
        cdef void* addr

        ret = []
        for offset, width, mask, value in prog:
            self._check(width, offset)

            width /= 8

            fullmask = 0xffffffffffffffff
            fullmask >>= (64-8*width)

            mask = mask&fullmask

            addr = ptr_add(self.base, offset)

            if mask!=fullmask:
                # something to read
                value &= ~mask

                if width==1:
                    V = ioread8(addr)
                elif width==2:
                    V = ioread16(addr)
                elif width==4:
                    V = ioread32(addr)
                else:
                    V = 0xdeadbeef

                value |= V&(~mask)

            if mask!=0:
                # something to write
                if width==1:
                    iowrite8(addr, value)
                elif width==2:
                    iowrite16(addr, value)
                elif width==4:
                    iowrite32(addr, value)

            ret.append(value)

        return ret

    def __getitem__(self, size_t offset):
        cdef size_t width
        cdef void* addr

        self._check(self.width, offset)

        width = self.width/8

        addr = ptr_add(self.base, offset)

        if width==1:
            return ioread8(addr)
        elif width==2:
            return ioread16(addr)
        elif width==4:
            return ioread32(addr)

    def __setitem__(self, size_t offset, size_t value):
        cdef size_t width
        cdef void* addr

        self._check(self.width, offset)

        width = self.width/8

        addr = ptr_add(self.base, offset)

        if width==1:
            iowrite8(addr, value)
        elif width==2:
            iowrite16(addr, value)
        elif width==4:
            iowrite32(addr, value)
