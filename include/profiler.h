#pragma once
#include <stdint.h>
#include <stdio.h>

#ifdef __riscv
// --- RISC-V bare-metal path ---
// rdcycle reads the mcycle CSR (Control and Status Register).
// It counts every clock cycle since reset. One inline asm instruction,
// result goes straight into a uint64_t.
static inline uint64_t profiler_now() {
    uint64_t cycles;
    asm volatile("rdcycle %0" : "=r"(cycles));
    return cycles;
}

// On bare-metal we have cycles, not nanoseconds.
// We still call the converter _ns_to_ms so main.cpp doesn't need ifdefs,
// but the unit is actually cycles. The percentages are what matter.
static inline double profiler_ns_to_ms(uint64_t cycles) {
    // Can't convert cycles to real time without knowing clock frequency.
    // Divide by 1e6 so the printed numbers are readable floats.
    return (double)cycles / 1.0e6;
}

#else
// --- Host (x86/ARM) path ---
// Normal clock_gettime, nanosecond precision.
#include <time.h>

static inline uint64_t profiler_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline double profiler_ns_to_ms(uint64_t ns) {
    return (double)ns / 1.0e6;
}
#endif