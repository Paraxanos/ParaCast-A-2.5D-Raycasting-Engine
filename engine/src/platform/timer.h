#ifndef PC_PLATFORM_TIMER_H
#define PC_PLATFORM_TIMER_H

#include <stdint.h>

/* Monotonic nanoseconds. Benchmarks only; the simulation never reads the clock. */
uint64_t pc_time_ns(void);

#endif
