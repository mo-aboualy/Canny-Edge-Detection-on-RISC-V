#pragma once

#include "image_io.h"

/**
 * @file gaussian_rvv.h
 * @brief RVV-accelerated 5×5 Gaussian blur — Phase 6.
 *
 * Three LMUL variants are provided for the Phase 6.2 sweep.
 * All three implement the same two-pass separable convolution:
 *   horizontal pass: uint8  → uint16  (kernel sum 16, max value 16×255=4080)
 *   vertical pass:   uint16 → uint32  (sum 16 again, divide >>8 → uint8)
 *
 * LMUL controls how many vector registers are grouped together per
 * operation, trading register pressure for throughput.  At VLEN=128:
 *   LMUL=1 → 16 elements/strip   (base accumulator u16m2, u32m4)
 *   LMUL=2 → 32 elements/strip   (base accumulator u16m4, u32m8)
 *   LMUL=4 → 64 elements/strip   (base accumulator u16m8, u32m16 — not used;
 *                                  instead we expose 4 via e8m4 loads)
 *
 * Boundary treatment: 2-pixel scalar border on all four sides (same as the
 * scalar separable path so outputs are bit-identical for interior pixels).
 *
 * Host / GoogleTest fallback: when __riscv_vector is not defined all three
 * functions call gaussian_blur_5x5_separable_1d() so every host test
 * compiles and passes without change.
 *
 * gaussian_blur_5x5_rvv() is an alias for the LMUL=1 variant and is the
 * function called by the main pipeline (main.cpp / run target).
 */

/** LMUL=1 — 16 bytes/strip at VLEN=128. Lowest register pressure. */
void gaussian_blur_5x5_rvv_lmul1(const Image& in, Image& out);

/** LMUL=2 — 32 bytes/strip at VLEN=128. Balanced throughput. */
void gaussian_blur_5x5_rvv_lmul2(const Image& in, Image& out);

/** LMUL=4 — 64 bytes/strip at VLEN=128. Highest throughput per call. */
void gaussian_blur_5x5_rvv_lmul4(const Image& in, Image& out);

/** Pipeline entry point — calls the LMUL=1 variant. */
void gaussian_blur_5x5_rvv(const Image& in, Image& out);
