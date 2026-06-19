#pragma once

#include "sobel.h"      /* GradientImage, gradient_free */
#include "image_io.h"   /* Image */

/**
 * @file sobel_rvv.h
 * @brief RVV-accelerated Sobel 3x3 gradient — Phase 6 public interface.
 *
 * Drop-in replacement for sobel_3x3().
 * When compiled for RISC-V with __riscv_vector defined the inner column
 * loop is strip-mined with RVV widening MACs (vwmaccsu).
 * On host builds (GoogleTest) the call falls through to sobel_3x3().
 *
 * Output contract: identical to sobel_3x3() —
 *   gx, gy : exact int16 Sobel gradients
 *   magnitude : sqrtf(gx^2+gy^2) clamped to [0,255], uint16
 *   border pixels (1px edge) : 0
 *   all buffers : aligned_alloc(64, ...), freed with gradient_free()
 */
void sobel_3x3_rvv(const Image& input, GradientImage& output);