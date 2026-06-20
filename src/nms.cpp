#include "nms.h"
#include <cstring>

void non_maximum_suppression(const uint8_t* mag, const uint8_t* dir,
                             uint8_t* out, int width, int height) {
    // Zero the output first — borders and suppressed pixels stay 0
    std::memset(out, 0, (size_t)width * height);

    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const int idx = y * width + x;
            const uint8_t m = mag[idx];

            uint8_t n1, n2; // the two neighbours along gradient direction

            switch (dir[idx]) {
                case 0: // 0° — horizontal edge, compare left/right
                    n1 = mag[idx - 1];
                    n2 = mag[idx + 1];
                    break;
                case 1: // 45° — compare bottom-left / top-right
                    n1 = mag[(y + 1) * width + (x - 1)];
                    n2 = mag[(y - 1) * width + (x + 1)];
                    break;
                case 2: // 90° — vertical edge, compare above/below
                    n1 = mag[(y - 1) * width + x];
                    n2 = mag[(y + 1) * width + x];
                    break;
                case 3: // 135° — compare top-left / bottom-right
                default:
                    n1 = mag[(y - 1) * width + (x - 1)];
                    n2 = mag[(y + 1) * width + (x + 1)];
                    break;
            }

            // Keep the pixel only if it is the local maximum
            if (m >= n1 && m >= n2) {
                out[idx] = m;
            }
            // else out[idx] remains 0 (already set by memset)
        }
    }
}
