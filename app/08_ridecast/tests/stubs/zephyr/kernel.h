#ifndef ZEPHYR_TEST_KERNEL_H
#define ZEPHYR_TEST_KERNEL_H
#include <stdint.h>
#define K_FOREVER 1
#define K_MUTEX_DEFINE(name) static int name
static inline void k_mutex_lock(int *m, int timeout) { (void)m; (void)timeout; }
static inline void k_mutex_unlock(int *m) { (void)m; }
extern int64_t fake_uptime;
static inline int64_t k_uptime_get(void) { return fake_uptime; }
#endif
