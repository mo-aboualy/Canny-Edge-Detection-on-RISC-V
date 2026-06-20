#pragma once
#include <cstdint>

/**
 * @file magnitude_rvv.h
 * @brief RVV-accelerated L1 gradient magnitude (Phase 6.5).
 *
 * Pass 1 (finding the global max for normalization) is fully
 * vectorized using RVV's widening add and reduction intrinsics.
 * Pass 2 (normalizing every pixel to [0,255]) is currently scalar --
 * it reuses pass 1's cached |Gx|+|Gy| values, so no redundant work
 * is repeated, but the per-pixel divide itself is not yet
 * vectorized. See the comment above pass 2 in magnitude_rvv.cpp.
 *
 * Falls back to the scalar gradient_magnitude_l1() on host /
 * non-RVV builds.
 */
void gradient_magnitude_l1_rvv(const int16_t* gx, const int16_t* gy,
                                uint8_t* magnitude, int width, int height);