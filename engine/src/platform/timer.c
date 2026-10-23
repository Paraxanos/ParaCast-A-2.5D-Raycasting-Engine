#define _POSIX_C_SOURCE 200809L

#include "platform/timer.h"

#include <time.h>

uint64_t pc_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
