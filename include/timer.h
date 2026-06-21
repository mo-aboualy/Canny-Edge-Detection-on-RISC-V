#pragma once  // ensures this header is only included once per compilation unit, preventing duplicate-definition errors

#include <stdint.h>  // gives us fixed-width integer types like uint64_t, guaranteed same size on every platform
#include <stdio.h>   // gives us printf, used at the end of the BENCHMARK macro to print results

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
    long tv_sec;   // whole seconds since an arbitrary starting point, written here by the kernel
    long tv_nsec;  // remaining nanoseconds (0 to 999,999,999), written here by the kernel
};

/**
 * get_ns() - returns a monotonically increasing nanosecond timestamp.
 *
 * Uses the raw RISC-V ecall interface:
 *   a7 = 113  (syscall number: clock_gettime)
 *   a0 = 1    (CLOCK_MONOTONIC)
 *   a1 = &ts  (pointer to struct timespec in memory)
 */
static inline uint64_t get_ns(void) {  // static inline: this function lives only in this translation unit and is small enough to inline at every call site
    struct _rvts ts = {0, 0};  // local struct that the kernel will fill in with the current time

    register long a7 __asm__("a7") = 113;
    // a7 is the SYSCALL NUMBER register on RISC-V Linux; 113 specifically means "clock_gettime"
    // changing this number would invoke a totally different syscall (e.g. 172 = getpid)

    register long a0 __asm__("a0") = 1;
    // a0 is the FIRST ARGUMENT register; here it selects WHICH clock to read
    // 1 = CLOCK_MONOTONIC (always counts forward, never jumps backward)
    // using 0 here would select CLOCK_REALTIME instead, which CAN jump and would break benchmarking

    register long a1 __asm__("a1") = (long)&ts;
    // a1 is the SECOND ARGUMENT register; here it's a pointer telling the kernel
    // WHERE in memory to write the resulting time value (into our 'ts' struct)

    __asm__ volatile ("ecall"
        : "+r"(a0)          // a0 is both an input (clock id) AND an output (syscall return code)
        : "r"(a1), "r"(a7)  // a1 and a7 are inputs only, never modified by the syscall
        : "memory");        // tells the compiler "memory may have changed here", preventing unsafe reordering/caching of reads around this instruction
    // "ecall" is RISC-V's single instruction meaning "ask the kernel/OS to perform a privileged operation for me"

    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    // combines whole seconds and remaining nanoseconds into one single number
    // multiplying by 1,000,000,000 converts seconds into nanoseconds before adding
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
        /* ^ timestamp taken right BEFORE the loop of repeated calls starts */ \
        for (int _i = 0; _i < (iters); _i++) { call; }                         \
        /* ^ runs the target function/statement 'iters' times back-to-back;   \
             running it many times and averaging smooths out QEMU's           \
             scheduling jitter into a stable, reproducible number */          \
        uint64_t _t1 = get_ns();                                                \
        /* ^ timestamp taken right AFTER all iterations finish */             \
        unsigned long long _avg =                                               \
            (unsigned long long)((_t1 - _t0) / (unsigned long long)(iters));   \
        /* ^ total elapsed time divided by number of iterations                \
             = average time spent on a single call */                        \
        printf("%-30s %llu ns/call\n", (name), _avg);                          \
        /* ^ %-30s left-aligns the label in a 30-character-wide field;        \
             %llu prints the average as an unsigned 64-bit integer */         \
    } while (0)
    // do-while(0) is a standard trick that makes this whole multi-line macro
    // behave exactly like ONE statement, so it's safe to use inside
    // if/else blocks without braces without causing bugs