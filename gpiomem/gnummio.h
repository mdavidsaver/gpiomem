#ifndef GNUMMIO_H
#define GNUMMIO_H

#include <stdint.h>

#define MAKEMMIO(W) static inline uint##W##_t ioread##W(void *addr) { \
    volatile uint##W##_t *iptr = addr; \
    uint32_t ret; \
    __asm__ __volatile__("":::"memory"); \
    ret = *iptr; \
    __asm__ __volatile__("":::"memory"); \
    return ret; \
} \
static inline void iowrite##W(void *addr, uint##W##_t val) { \
    volatile uint##W##_t *iptr = addr; \
    __asm__ __volatile__("":::"memory"); \
    *iptr = val; \
    __asm__ __volatile__("":::"memory"); \
}

MAKEMMIO(8)
MAKEMMIO(16)
MAKEMMIO(32)

static inline
void* ptr_add(void* base, size_t offset) {
    return offset + (char*)base;
}

#endif /* GNUMMIO_H */
