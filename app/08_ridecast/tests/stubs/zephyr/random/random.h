#ifndef ZEPHYR_TEST_RANDOM_H
#define ZEPHYR_TEST_RANDOM_H
#include <stdint.h>
#include <stddef.h>
static inline uint32_t sys_rand32_get(void) { return 123456U; }
static inline void sys_rand_get(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(i + 16U);
}
#endif
