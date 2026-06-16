#pragma once
#include <time.h>
#include <stdint.h>
#include <stdio.h>

static inline uint64_t get_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define BENCHMARK(name, iters, call) do {                              \
    uint64_t _t0 = get_ns();                                           \
    for (int _i = 0; _i < (iters); _i++) { call; }                    \
    uint64_t _t1 = get_ns();                                           \
    unsigned long long _avg = (_t1-_t0) / (unsigned long long)(iters); \
    printf("%-30s %llu ns/call\n", name, _avg);                       \
} while(0)
