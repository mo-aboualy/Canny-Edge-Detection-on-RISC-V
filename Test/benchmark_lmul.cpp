#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "image_io.h"
#include "gaussian_blur.h"
#include "gaussian_rvv.h"
#include "timer.h"

static constexpr int W = 512;
static constexpr int H = 512;
static constexpr int ITERS = 200;

static Image alloc_img(int w, int h) {
    Image img;
    img.width = w; 
    img.height = h;
    size_t sz = static_cast<size_t>(w) * h;
    img.pixels = static_cast<uint8_t*>(aligned_alloc(64, (sz + 63) & ~63));
    return img;
}

static void fill_pattern(Image& img) {
    for (uint32_t y = 0; y < img.height; ++y)
        for (uint32_t x = 0; x < img.width; ++x)
            img.pixels[y * img.width + x] = static_cast<uint8_t>((x + y) & 0xFF);
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
    Image src = alloc_img(W, H);
    fill_pattern(src);

    Image ref   = alloc_img(W, H);
    Image lmul1 = alloc_img(W, H);
    Image lmul2 = alloc_img(W, H);
    Image lmul4 = alloc_img(W, H);

    // This tests against your unmodified spatial 2D reference implementation
    gaussian_blur_5x5_spatial_2d(src, ref);
    gaussian_blur_5x5_rvv_lmul1(src, lmul1);
    gaussian_blur_5x5_rvv_lmul2(src, lmul2);
    gaussian_blur_5x5_rvv_lmul4(src, lmul4);

    const size_t n = static_cast<size_t>(W) * H;
    std::printf("============================================================\n");
    std::printf(" Phase 6.2: LMUL Sweep — Correctness (Spatial 2D Matrix)\n");
    std::printf("============================================================\n");
    
    int d1 = max_diff(ref.pixels, lmul1.pixels, n);
    int d2 = max_diff(ref.pixels, lmul2.pixels, n);
    int d4 = max_diff(ref.pixels, lmul4.pixels, n);

    std::printf("LMUL=1 max |diff| = %d  -> %s\n", d1, d1 == 0 ? "PASS" : "FAIL");
    std::printf("LMUL=2 max |diff| = %d  -> %s\n", d2, d2 == 0 ? "PASS" : "FAIL");
    std::printf("LMUL=4 max |diff| = %d  -> %s\n", d4, d4 == 0 ? "PASS" : "FAIL");

    std::printf("\n============================================================\n");
    std::printf(" Phase 6.2: LMUL Sweep — Timing (%d iterations)\n", ITERS);
    std::printf("============================================================\n");
    BENCHMARK("Gaussian RVV LMUL=1", ITERS, gaussian_blur_5x5_rvv_lmul1(src, lmul1));
    BENCHMARK("Gaussian RVV LMUL=2", ITERS, gaussian_blur_5x5_rvv_lmul2(src, lmul2));
    BENCHMARK("Gaussian RVV LMUL=4", ITERS, gaussian_blur_5x5_rvv_lmul4(src, lmul4));

    // Safe direct deallocations of the raw aligned_alloc arrays
    std::free(src.pixels); 
    std::free(ref.pixels);
    std::free(lmul1.pixels); 
    std::free(lmul2.pixels); 
    std::free(lmul4.pixels);
    
    return 0;
}
