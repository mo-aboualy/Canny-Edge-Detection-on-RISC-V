#include "magnitude.h"

#include <cmath>
#include <cstdint>

namespace {

static inline int32_t abs_i16_to_i32(int16_t value) {
    return value < 0 ? -static_cast<int32_t>(value)
                     :  static_cast<int32_t>(value);
}

static inline uint8_t normalize_to_u8_u32(uint32_t value, uint32_t max_value) {
    if (max_value == 0) {
        return 0;
    }

    const uint32_t scaled = (value * 255U) / max_value;
    return scaled > 255U ? 255U : static_cast<uint8_t>(scaled);
}

} // namespace

void gradient_magnitude_l1(
    const int16_t* gx,
    const int16_t* gy,
    uint8_t* magnitude,
    int width,
    int height
) {
    const int count = width * height;

    /*
     * Pass 1:
     * Find maximum L1 magnitude.
     *
     * L1 = |Gx| + |Gy|
     *
     * This is integer-only and faster than L2.
     */
    uint32_t max_mag = 0;

    for (int i = 0; i < count; ++i) {
        const uint32_t ax = static_cast<uint32_t>(abs_i16_to_i32(gx[i]));
        const uint32_t ay = static_cast<uint32_t>(abs_i16_to_i32(gy[i]));
        const uint32_t mag = ax + ay;

        if (mag > max_mag) {
            max_mag = mag;
        }
    }

    /*
     * Pass 2:
     * Normalize L1 magnitude to [0, 255].
     */
    for (int i = 0; i < count; ++i) {
        const uint32_t ax = static_cast<uint32_t>(abs_i16_to_i32(gx[i]));
        const uint32_t ay = static_cast<uint32_t>(abs_i16_to_i32(gy[i]));
        const uint32_t mag = ax + ay;

        magnitude[i] = normalize_to_u8_u32(mag, max_mag);
    }
}

void gradient_magnitude_l2(
    const int16_t* gx,
    const int16_t* gy,
    uint8_t* magnitude,
    int width,
    int height
) {
    const int count = width * height;

    /*
     * Pass 1:
     * Find maximum squared L2 magnitude.
     *
     * L2 = sqrt(Gx^2 + Gy^2)
     *
     * For normalization, sqrt is monotonic, so the maximum L2 magnitude
     * corresponds to the maximum squared magnitude. We still compute sqrt
     * in pass 2 to produce true L2 values before scaling.
     */
    uint32_t max_sq = 0;

    for (int i = 0; i < count; ++i) {
        const int32_t x = static_cast<int32_t>(gx[i]);
        const int32_t y = static_cast<int32_t>(gy[i]);

        const uint32_t sq =
            static_cast<uint32_t>(x * x + y * y);

        if (sq > max_sq) {
            max_sq = sq;
        }
    }

    if (max_sq == 0) {
        for (int i = 0; i < count; ++i) {
            magnitude[i] = 0;
        }
        return;
    }

    const double max_l2 = std::sqrt(static_cast<double>(max_sq));

    /*
     * Pass 2:
     * Compute true L2 magnitude and normalize to [0, 255].
     *
     * This is slower than L1 because it uses sqrt().
     */
    for (int i = 0; i < count; ++i) {
        const int32_t x = static_cast<int32_t>(gx[i]);
        const int32_t y = static_cast<int32_t>(gy[i]);

        const double l2 =
            std::sqrt(static_cast<double>(x * x + y * y));

        const double scaled = (l2 * 255.0) / max_l2;

        if (scaled <= 0.0) {
            magnitude[i] = 0;
        } else if (scaled >= 255.0) {
            magnitude[i] = 255;
        } else {
            magnitude[i] = static_cast<uint8_t>(scaled);
        }
    }
}

void gradient_magnitude(
    const int16_t* gx,
    const int16_t* gy,
    uint8_t* magnitude,
    int width,
    int height
) {
    /*
     * Keep the existing pipeline behavior simple:
     * default magnitude = L1.
     */
    gradient_magnitude_l1(gx, gy, magnitude, width, height);
}
