#pragma once
#include <cstdint>

/**
 * @file hysteresis.h
 * @brief Hysteresis thresholding for Canny Edge Detection.
 *
 * Classifies each pixel in the NMS output as:
 *   - Strong edge  (>= high_thresh)  → output 255
 *   - Weak edge    (>= low_thresh)   → output 128 provisionally
 *   - Non-edge     (< low_thresh)    → output 0
 *
 * Then connectivity is resolved: a weak pixel is kept (255) only if it
 * is 8-connected to at least one strong pixel, otherwise it is discarded (0).
 *
 * Typical thresholds: low = 0.4 * high, high = ~50–100 for uint8 magnitude.
 *
 * @param nms        Input NMS-thinned magnitude map.
 * @param out        Output binary edge map (255 = edge, 0 = non-edge).
 * @param width      Image width in pixels.
 * @param height     Image height in pixels.
 * @param low_thresh Lower hysteresis threshold  [0..255].
 * @param high_thresh Upper hysteresis threshold [0..255].
 */
void hysteresis_threshold(const uint8_t* nms, uint8_t* out,
                          int width, int height,
                          uint8_t low_thresh, uint8_t high_thresh);
