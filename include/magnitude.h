#ifndef MAGNITUDE_H
#define MAGNITUDE_H

#include <cstdint>

/**
 * @brief Computes L1 gradient magnitude.
 *
 * Formula:
 *   L1 = |Gx| + |Gy|
 *
 * The output is normalized to [0, 255] using two passes:
 *   1. Find maximum magnitude.
 *   2. Scale all magnitudes relative to that maximum.
 */
void gradient_magnitude_l1(
    const int16_t* gx,
    const int16_t* gy,
    uint8_t* magnitude,
    int width,
    int height
);

/**
 * @brief Computes L2 gradient magnitude.
 *
 * Formula:
 *   L2 = sqrt(Gx*Gx + Gy*Gy)
 *
 * The output is normalized to [0, 255] using two passes:
 *   1. Find maximum magnitude.
 *   2. Scale all magnitudes relative to that maximum.
 */
void gradient_magnitude_l2(
    const int16_t* gx,
    const int16_t* gy,
    uint8_t* magnitude,
    int width,
    int height
);

/**
 * @brief Backward-compatible default magnitude function.
 *
 * Existing pipeline code can keep calling gradient_magnitude().
 * It uses L1 because L1 is the faster embedded-friendly approximation.
 */
void gradient_magnitude(
    const int16_t* gx,
    const int16_t* gy,
    uint8_t* magnitude,
    int width,
    int height
);

#endif // MAGNITUDE_H
