#include "direction.h"
#include <cstdlib>

void gradient_direction(const int16_t* Gx, const int16_t* Gy,
                        uint8_t* dir, int width, int height) {
    int n = width * height;
    for (int i = 0; i < n; i++) {
        int32_t ax = std::abs((int32_t)Gx[i]);
        int32_t ay = std::abs((int32_t)Gy[i]);
        if (ay * 5 < ax * 2)        dir[i] = 0; // 0°
        else if (Gx[i] * Gy[i] > 0) dir[i] = 1; // 45°
        else                         dir[i] = 3; // 135°
        if (ax * 5 < ay * 2)        dir[i] = 2; // 90°
    }
}
