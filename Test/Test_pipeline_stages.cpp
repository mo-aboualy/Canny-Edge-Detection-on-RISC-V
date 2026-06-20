/**
 * @file Test_pipeline_stages.cpp
 * @brief Runs the full Canny pipeline (Gaussian -> Sobel -> Direction ->
 *        Magnitude -> NMS -> Hysteresis) on one input image and writes
 *        EVERY intermediate stage out as its own raw grayscale file, so
 *        they can all be viewed/compared side by side.
 *
 * Usage:
 *   ./test_pipeline_stages <input.raw> <width> <height> [out_dir] [low] [high]
 *
 * Example:
 *   ./test_pipeline_stages input_512.raw 512 512 Tool/stages 50 100
 *
 * Output files written into out_dir (default "Tool/stages"):
 *   01_input.raw        - original input, unchanged
 *   02_blurred.raw       - after 5x5 Gaussian blur
 *   03_gx.raw            - horizontal Sobel gradient, remapped to [0,255]
 *   04_gy.raw            - vertical Sobel gradient, remapped to [0,255]
 *   05_magnitude.raw     - Sobel gradient magnitude, clamped to [0,255]
 *   06_direction.raw     - direction bin (0..3), color-coded RGB
 *                            (red=0deg, green=45deg, blue=90deg, yellow=135deg)
 *                            NOTE: this file is width*height*3 bytes (RGB),
 *                            unlike the other stages which are width*height.
 *   07_nms.raw           - magnitude after non-maximum suppression
 *   08_hysteresis.raw    - final binary edge map (0 or 255)
 *
 * All files are exactly width*height bytes, row-major, 1 byte/pixel,
 * so Tool/view_result.py (or any raw-image viewer) can load them directly.
 */

#include "image_io.h"
#include "gaussian_blur.h"
#include "sobel.h"
#include "magnitude.h"
#include "direction.h"
#include "nms.h"
#include "hysteresis.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <sys/stat.h>

// Allocate a 64-byte aligned Image buffer of the given size.
static Image alloc_image(uint32_t w, uint32_t h) {
    Image img;
    img.width  = w;
    img.height = h;
    size_t sz  = (size_t)w * h;
    size_t aligned_sz = (sz + 63) & ~63;
    img.pixels = (uint8_t*)aligned_alloc(64, aligned_sz);
    if (!img.pixels) {
        fprintf(stderr, "ERROR: aligned_alloc failed for %ux%u image\n", w, h);
        exit(1);
    }
    return img;
}

// Write a raw uint8_t buffer (not wrapped in an Image) directly to disk.
static bool write_raw(const char* path, const uint8_t* data, uint32_t w, uint32_t h) {
    Image tmp;
    tmp.width  = w;
    tmp.height = h;
    tmp.pixels = const_cast<uint8_t*>(data);
    return image_write(path, tmp);
}

// Remap a signed gradient buffer (e.g. Gx, Gy in roughly [-1020, 1020])
// to an unsigned [0,255] image for visualization:
//   0       -> mid-gray (128)
//   negative -> darker
//   positive -> brighter
// Clamped at the extremes so strong edges still show as near-black/near-white.
static void remap_signed_to_u8(const int16_t* src, uint8_t* dst, size_t count) {
    for (size_t i = 0; i < count; i++) {
        int32_t v = (int32_t)src[i] / 4 + 128; // /4 roughly maps [-1020,1020] -> [-255,255]
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        dst[i] = (uint8_t)v;
    }
}

// Scale 4-bin direction values (0,1,2,3) to distinct colors so each
// orientation is immediately recognizable rather than just a shade of gray:
//   0 (0 deg, horizontal edge)   -> Red
//   1 (45 deg)                   -> Green
//   2 (90 deg, vertical edge)    -> Blue
//   3 (135 deg)                  -> Yellow
// Output is interleaved RGB, 3 bytes/pixel (width*height*3 total bytes).
static void remap_direction_to_rgb(const uint8_t* dir, uint8_t* dst_rgb, size_t count) {
    static const uint8_t palette[4][3] = {
        {255,   0,   0}, // 0 deg  - red
        {  0, 255,   0}, // 45 deg - green
        {  0,   0, 255}, // 90 deg - blue
        {255, 255,   0}, // 135 deg- yellow
    };
    for (size_t i = 0; i < count; i++) {
        uint8_t bin = dir[i] & 0x03; // guard against out-of-range values
        dst_rgb[i*3 + 0] = palette[bin][0];
        dst_rgb[i*3 + 1] = palette[bin][1];
        dst_rgb[i*3 + 2] = palette[bin][2];
    }
}

// Writes an interleaved RGB buffer (3 bytes/pixel) directly to disk.
// No header, just width*height*3 raw bytes — same "no header" convention
// as the grayscale stages, just 3x the size per pixel.
static bool write_raw_rgb(const char* path, const uint8_t* rgb, uint32_t w, uint32_t h) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t total = (size_t)w * h * 3;
    size_t n = fwrite(rgb, 1, total, f);
    fclose(f);
    return n == total;
}

static void make_dir_if_needed(const char* dir) {
    mkdir(dir, 0755); // ignore error if it already exists
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input.raw> <width> <height> [out_dir] [low] [high]\n", argv[0]);
        fprintf(stderr, "Example: %s input_512.raw 512 512 Tool/stages 50 100\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const uint32_t W = (uint32_t)std::atoi(argv[2]);
    const uint32_t H = (uint32_t)std::atoi(argv[3]);
    const char* out_dir = (argc > 4) ? argv[4] : "Tool/stages";
    const uint8_t low_thresh  = (argc > 5) ? (uint8_t)std::atoi(argv[5]) : 50;
    const uint8_t high_thresh = (argc > 6) ? (uint8_t)std::atoi(argv[6]) : 100;

    make_dir_if_needed(out_dir);

    char path[1024];

    printf("----------------------------------------\n");
    printf(" Full Pipeline Stage Dump\n");
    printf("----------------------------------------\n");
    printf(" Input    : %s\n", input_file);
    printf(" Size     : %ux%u\n", W, H);
    printf(" Out dir  : %s\n", out_dir);
    printf(" Thresh   : low=%u high=%u\n", low_thresh, high_thresh);
    printf("----------------------------------------\n");

    // ── 1. LOAD INPUT ──────────────────────────────────────────────
    Image input_img;
    if (!image_read(input_file, W, H, input_img)) {
        fprintf(stderr, "Error: Could not read '%s'.\n", input_file);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/01_input.raw", out_dir);
    write_raw(path, input_img.pixels, W, H);
    printf("Stage 1: input            -> %s\n", path);

    // ── 2. GAUSSIAN BLUR ───────────────────────────────────────────
    Image blurred = alloc_image(W, H);
    gaussian_blur_5x5(input_img, blurred);
    snprintf(path, sizeof(path), "%s/02_blurred.raw", out_dir);
    write_raw(path, blurred.pixels, W, H);
    printf("Stage 2: gaussian blur    -> %s\n", path);

    // ── 3. SOBEL (gx, gy, magnitude) ───────────────────────────────
    GradientImage grad;
    grad.gx = grad.gy = nullptr;
    grad.magnitude = nullptr;
    grad.width = W;
    grad.height = H;
    sobel_3x3(blurred, grad);

    size_t count = (size_t)W * H;
    uint8_t* gx_u8 = (uint8_t*)aligned_alloc(64, (count + 63) & ~63);
    uint8_t* gy_u8 = (uint8_t*)aligned_alloc(64, (count + 63) & ~63);
    remap_signed_to_u8(grad.gx, gx_u8, count);
    remap_signed_to_u8(grad.gy, gy_u8, count);

    snprintf(path, sizeof(path), "%s/03_gx.raw", out_dir);
    write_raw(path, gx_u8, W, H);
    printf("Stage 3: sobel gx         -> %s\n", path);

    snprintf(path, sizeof(path), "%s/04_gy.raw", out_dir);
    write_raw(path, gy_u8, W, H);
    printf("Stage 4: sobel gy         -> %s\n", path);

    // ── 4. MAGNITUDE (recomputed via magnitude.cpp, L1) ────────────
    uint8_t* mag_u8 = (uint8_t*)aligned_alloc(64, (count + 63) & ~63);
    gradient_magnitude(grad.gx, grad.gy, mag_u8, (int)W, (int)H);
    snprintf(path, sizeof(path), "%s/05_magnitude.raw", out_dir);
    write_raw(path, mag_u8, W, H);
    printf("Stage 5: magnitude (L1)   -> %s\n", path);

    // ── 5. DIRECTION ────────────────────────────────────────────────
    uint8_t* dir_raw = (uint8_t*)aligned_alloc(64, (count + 63) & ~63);
    gradient_direction(grad.gx, grad.gy, dir_raw, (int)W, (int)H);
    uint8_t* dir_rgb = (uint8_t*)aligned_alloc(64, (count * 3 + 63) & ~63);
    remap_direction_to_rgb(dir_raw, dir_rgb, count);
    snprintf(path, sizeof(path), "%s/06_direction.raw", out_dir);
    write_raw_rgb(path, dir_rgb, W, H);
    printf("Stage 6: direction (color)-> %s\n", path);

    // ── 6. NON-MAXIMUM SUPPRESSION ──────────────────────────────────
    uint8_t* nms_u8 = (uint8_t*)aligned_alloc(64, (count + 63) & ~63);
    non_maximum_suppression(mag_u8, dir_raw, nms_u8, (int)W, (int)H);
    snprintf(path, sizeof(path), "%s/07_nms.raw", out_dir);
    write_raw(path, nms_u8, W, H);
    printf("Stage 7: NMS              -> %s\n", path);

    // ── 7. HYSTERESIS THRESHOLDING ──────────────────────────────────
    uint8_t* edges_u8 = (uint8_t*)aligned_alloc(64, (count + 63) & ~63);
    hysteresis_threshold(nms_u8, edges_u8, (int)W, (int)H, low_thresh, high_thresh);
    snprintf(path, sizeof(path), "%s/08_hysteresis.raw", out_dir);
    write_raw(path, edges_u8, W, H);
    printf("Stage 8: hysteresis       -> %s\n", path);

    printf("----------------------------------------\n");
    printf(" Done. All stages written to %s/\n", out_dir);
    printf(" View with:\n");
    printf("   python3 Tool/view_pipeline.py %s %u %u\n", out_dir, W, H);
    printf("----------------------------------------\n");

    // ── CLEANUP ──────────────────────────────────────────────────────
    free(gx_u8);
    free(gy_u8);
    free(mag_u8);
    free(dir_raw);
    free(dir_rgb);
    free(nms_u8);
    free(edges_u8);
    gradient_free(grad);
    image_free(input_img);
    image_free(blurred);

    return 0;
}