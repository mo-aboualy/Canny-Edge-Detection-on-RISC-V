#include "magnitude.h"
#include <cstdlib>
#include <algorithm>

void gradient_magnitude(const int16_t* Gx, const int16_t* Gy,
                        uint8_t* mag, int width, int height) {
    int n = width * height;
    int32_t max_val = 1;
    for (int i = 0; i < n; i++) {
        int32_t m = std::abs((int32_t)Gx[i]) + std::abs((int32_t)Gy[i]);
        if (m > max_val) max_val = m;
    }
    for (int i = 0; i < n; i++) {
        int32_t m = std::abs((int32_t)Gx[i]) + std::abs((int32_t)Gy[i]);
        mag[i] = (uint8_t)(m * 255 / max_val);
    }
}
