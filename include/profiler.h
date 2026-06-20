#pragma once
#include <stdint.h>   // for uint64_t — large counters need 64 bits to avoid overflow
#include <stdio.h>

// __riscv is automatically defined by the compiler itself when compiling
// for a RISC-V target (e.g. via riscv64-...-g++). This #ifdef lets the
// SAME header file produce two completely different implementations
// depending on which compiler is currently being used — no manual
// switching required by the code that calls these functions.
#ifdef __riscv
// --- RISC-V bare-metal path ---
// rdcycle reads the mcycle CSR (Control and Status Register).
// It counts every clock cycle since reset. One inline asm instruction,
// result goes straight into a uint64_t.
static inline uint64_t profiler_now() {
    uint64_t cycles;
    // No C/C++ standard function can read a CPU's internal cycle counter —
    // this requires dropping down to raw assembly. "rdcycle %0" is a single
    // RISC-V instruction; "=r"(cycles) tells the compiler "put the result
    // into the 'cycles' variable, via any general-purpose register."
    asm volatile("rdcycle %0" : "=r"(cycles));
    return cycles;
}

// On bare-metal we have cycles, not nanoseconds.
// We still call the converter _ns_to_ms so main.cpp doesn't need ifdefs,
// but the unit is actually cycles. The percentages are what matter.
static inline double profiler_ns_to_ms(uint64_t cycles) {
    // Can't convert cycles to real time without knowing clock frequency.
    // Divide by 1e6 so the printed numbers are readable floats.
    // IMPORTANT: this is NOT a real millisecond value under QEMU — QEMU
    // does not back rdcycle with a meaningful, fixed clock frequency.
    // Only RELATIVE comparisons between two profiler_now() calls (e.g.
    // percentage breakdowns) are trustworthy here, not the absolute number.
    return (double)cycles / 1.0e6;
}

#else
// --- Host (x86/ARM) path ---
// Normal clock_gettime, nanosecond precision.
// Taken when __riscv is NOT defined — i.e. compiling natively for the
// development machine (host tests, host visualizers, etc.).
#include <time.h>

static inline uint64_t profiler_now() {
    struct timespec ts;
    // CLOCK_MONOTONIC: a clock that only ever moves forward, unaffected by
    // system clock adjustments (NTP sync, manual time changes, etc.) —
    // the correct choice for measuring elapsed durations.
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // timespec splits time into whole seconds (tv_sec) and the leftover
    // sub-second part in nanoseconds (tv_nsec). Combine both into one
    // single nanosecond-resolution 64-bit count.
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline double profiler_ns_to_ms(uint64_t ns) {
    // On the host this is a REAL, exact unit conversion:
    // 1 millisecond = 1,000,000 nanoseconds.
    return (double)ns / 1.0e6;
}
#endif