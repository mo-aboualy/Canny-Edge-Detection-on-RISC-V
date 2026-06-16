#ifndef GAUSSIAN_BLUR_H
#define GAUSSIAN_BLUR_H
#include "image_io.h"
#include <cstdlib>
#include <string.h>

/**
 * @brief Version 1: Gaussian Blur WITH boundary check inside kernel loop.
 * The if-statement prevents compiler auto-vectorization.
 * Expected vec_report: "not vectorized: control flow in loop"
 */
void gaussian_blur_5x5(const Image& in, Image& out);

/**
 * @brief Version 2: Gaussian Blur WITH pre-padding, NO boundary check.
 * Branch-free inner loop allows compiler to attempt vectorization.
 * Expected vec_report: "not vectorized: complicated access pattern"
 * Proves manual RVV intrinsics are needed for Phase 5.
 */
void gaussian_blur_padded(const Image& in, Image& out);

#endif // GAUSSIAN_BLUR_H
