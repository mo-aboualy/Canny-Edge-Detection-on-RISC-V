#pragma once

#include <stdint.h>
#include <stdio.h>

/**
 * @file timer.h
 * @brief Monotonic wall-clock timer for RISC-V / QEMU user-mode benchmarking.
 *
 * riscv64-unknown-elf targets bare-metal (newlib), so POSIX clock_gettime()
 * is not linked by the C library.  Under QEMU user-mode the Linux kernel ABI
 * is available, so we invoke syscall 113 (clock_gettime) directly.
 *
 * CLOCK_MONOTONIC = 1  (Linux ABI constant, same on every architecture)
 *
 * QEMU is not cycle-accurate.  Absolute ns values are meaningless; only
 * *relative* comparisons (-O0 vs -O3, scalar vs RVV) are valid because the
 * emulated instruction count changes.  Run each kernel 100+ iterations to
 * average out scheduling jitter.
 */

/* Matches struct timespec in the Linux ABI (64-bit tv_sec + 64-bit tv_nsec) */
struct _rvts {
    long tv_sec;
    long tv_nsec;
};

/**
 * get_ns() - returns a monotonically increasing nanosecond timestamp.
 *
 * Uses the raw RISC-V ecall interface:
 *   a7 = 113  (syscall number: clock_gettime)
 *   a0 = 1    (CLOCK_MONOTONIC)
 *   a1 = &ts  (pointer to struct timespec in memory)
 */
static inline uint64_t get_ns(void) {
    struct _rvts ts = {0, 0};
    register long a7 __asm__("a7") = 113;          /* __NR_clock_gettime */
    register long a0 __asm__("a0") = 1;            /* CLOCK_MONOTONIC    */
    register long a1 __asm__("a1") = (long)&ts;    /* struct timespec *  */
    __asm__ volatile ("ecall"
        : "+r"(a0)
        : "r"(a1), "r"(a7)
        : "memory");
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * BENCHMARK(name, iters, call)
 *
 * Times `call` repeated `iters` times and prints the average ns/call.
 * The do-while(0) wrapper makes this safe to use anywhere a statement fits.
 */
#define BENCHMARK(name, iters, call)                                            \
    do {                                                                        \
        uint64_t _t0 = get_ns();                                                \
        for (int _i = 0; _i < (iters); _i++) { call; }                         \
        uint64_t _t1 = get_ns();                                                \
        unsigned long long _avg =                                               \
            (unsigned long long)((_t1 - _t0) / (unsigned long long)(iters));   \
        printf("%-30s %llu ns/call\n", (name), _avg);                          \
    } while (0)
