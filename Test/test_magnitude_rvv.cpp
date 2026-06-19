/**
 * @file test_magnitude_rvv.cpp
 * @brief Phase 6.5 equivalence test: scalar L1 magnitude vs RVV L1 magnitude.
 *
 * Deliberately uses a non-power-of-two image size (97x83) to force the
 * strip-mine tail/remainder case to execute — the most common place
 * where VLA bugs hide. Must pass at VLEN=128, 256, and 512.
 */

#include "magnitude.h"
#include "magnitude_rvv.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

static constexpr int W = 97;
static constexpr int H = 83;
static constexpr size_t ALIGNMENT = 64;

static inline size_t align64(size_t n) {
    return (n + 63) & ~static_cast<size_t>(63);
}

static int max_diff(const uint8_t* a, const uint8_t* b, size_t n) {
    int m = 0;
    for (size_t i = 0; i < n; ++i) {
        int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        if (d < 0) d = -d;
        if (d > m) m = d;
    }
    return m;
}

int main() {
    const size_t N = static_cast<size_t>(W) * H;

    int16_t* gx  = static_cast<int16_t*>(aligned_alloc(ALIGNMENT, align64(N * sizeof(int16_t))));
    int16_t* gy  = static_cast<int16_t*>(aligned_alloc(ALIGNMENT, align64(N * sizeof(int16_t))));
    uint8_t* ref = static_cast<uint8_t*>(aligned_alloc(ALIGNMENT, align64(N)));
    uint8_t* rvv = static_cast<uint8_t*>(aligned_alloc(ALIGNMENT, align64(N)));

    if (!gx || !gy || !ref || !rvv) {
        std::printf("FATAL: allocation failed\n");
        return 1;
    }

    // Fill with a deterministic pattern that exercises both
    // small and large gradient values, including zeros and max.
    for (size_t i = 0; i < N; ++i) {
        gx[i] = static_cast<int16_t>((i * 7 + 13) % 511 - 255);
        gy[i] = static_cast<int16_t>((i * 3 + 97) % 511 - 255);
    }
    // Force a few corner cases explicitly
    gx[0] = 0;   gy[0] = 0;
    gx[1] = 255; gy[1] = 255;
    gx[2] = -255; gy[2] = -255;

    std::memset(ref, 0, N);
    std::memset(rvv, 0, N);

    gradient_magnitude_l1(gx, gy, ref, W, H);
    gradient_magnitude_l1_rvv(gx, gy, rvv, W, H);

    const int diff = max_diff(ref, rvv, N);

    std::printf("============================================================\n");
    std::printf(" Phase 6.5 Magnitude RVV Equivalence Test\n");
    std::printf(" Image size: %dx%d (non-power-of-two, tests tail handling)\n", W, H);
    std::printf("============================================================\n");
    std::printf("Scalar L1 vs RVV L1 max|diff| = %d  -> %s\n",
                diff, diff == 0 ? "PASS" : "FAIL");

    std::free(gx);
    std::free(gy);
    std::free(ref);
    std::free(rvv);

    return diff == 0 ? 0 : 1;
}