#pragma once  // ensures this header is only processed once per compilation unit

#include <stdint.h>  // gives us uint8_t, int16_t, int32_t — fixed-width types guaranteeing the same size on every platform
#include <stdlib.h>  // gives us calloc() and free(), used by the padded version below to manage the temporary buffer
#include <string.h>  // gives us memcpy(), used by the padded version to copy the real image into the padded buffer

static const int16_t GAUSS[5][5] = {
    // static: only visible inside this file, avoids naming conflicts with other files
    // const: values never change at runtime, lets the compiler store this in read-only memory
    // int16_t: each coefficient fits comfortably in 16 bits (max value here is 15)
    { 2,  4,  5,  4,  2},   // row 0 of the kernel: outer ring, low weight (far from center)
    { 4,  9, 12,  9,  4},   // row 1: second ring, medium weight
    { 5, 12, 15, 12,  5},   // row 2: middle row, highest weight (15) sits at the very center
    { 4,  9, 12,  9,  4},   // row 3: mirrors row 1 (Gaussian kernels are symmetric)
    { 2,  4,  5,  4,  2}    // row 4: mirrors row 0
};
// all 25 values sum to 273 — this number reappears later as the normalization divisor;
// if these weights ever change, the divisor used below MUST be recalculated to match

// Version 1: WITH boundary check (compiler cannot vectorize)
void gaussian_blur_scalar(const uint8_t* in, uint8_t* out,
                          int W, int H) {
    // in:  pointer to the source image pixels (read-only, won't be modified)
    // out: pointer to the destination buffer (caller must pre-allocate W*H bytes)
    // W:   image width in pixels — change this to process a wider/narrower image
    // H:   image height in pixels — change this to process a taller/shorter image

    for (int r = 0; r < H; r++) {
        // r = current row being processed; loops over every row in the image, top to bottom
        for (int c = 0; c < W; c++) {
            // c = current column being processed; loops over every column in the current row
            // together, r and c visit every single pixel in the image exactly once

            int32_t acc = 0;
            // accumulator: holds the running sum of pixel*weight for this pixel's neighborhood
            // int32_t (not int16_t) because worst case ~25 * 255 * 15 ≈ 95,625, which overflows int16_t

            for (int kr = -2; kr <= 2; kr++) {
                // kr = kernel row offset, from -2 to +2 (5 values), matching the 5x5 kernel's vertical reach
                for (int kc = -2; kc <= 2; kc++) {
                    // kc = kernel column offset, from -2 to +2, matching the kernel's horizontal reach

                    int sr = r + kr, sc = c + kc;
                    // sr/sc = the ACTUAL row/column in the image this kernel position wants to read
                    // these can fall outside the image when r/c are near the edges

                    if (sr < 0 || sr >= H || sc < 0 || sc >= W)
                        continue;
                    // boundary check: if the neighbor pixel is outside the image, skip it entirely
                    // (equivalent to treating it as value 0 — "zero padding")
                    // THIS IS THE LINE that blocks GCC's auto-vectorizer: a conditional branch
                    // inside the innermost loop is reported as "control flow in loop" and prevents
                    // the compiler from turning this into vector instructions

                    acc += (int32_t)in[sr * W + sc]
                           * GAUSS[kr+2][kc+2];
                    // in[sr * W + sc]: converts the 2D position (sr, sc) into a 1D array index,
                    //                  since the image is stored as one flat row-major array
                    // (int32_t): widens the uint8_t pixel value before multiplying, preventing overflow
                    // GAUSS[kr+2][kc+2]: the +2 shifts kr/kc (range -2..2) into valid array
                    //                    indices (0..4), since arrays can't use negative indices
                }
            }
            int32_t v = acc / 273;
            // dividing by 273 (the kernel's coefficient sum) normalizes the result back
            // into a sensible brightness range — if the kernel weights change, this 273 must change too

            out[r * W + c] = v < 0 ? 0 : (v > 255 ? 255 : v);
            // clamps v into the valid uint8_t range [0, 255] before writing to the output buffer
            // (manual equivalent of std::clamp)
        }
    }
}

// Version 2: WITH pre-padding (compiler CAN vectorize)
void gaussian_blur_padded(const uint8_t* in, uint8_t* out,
                          int W, int H) {
    // same 4 parameters as above: in (source), out (destination), W (width), H (height)

    int PW = W + 4, PH = H + 4;
    // PW/PH = "padded width/height" — 4 extra pixels added to each dimension
    // why 4? the 5x5 kernel reaches 2 pixels outward in every direction (left+right = 2+2 = 4)
    // if the kernel were 7x7 instead, this would need to become +6 (3 pixels each side)

    uint8_t* pad = (uint8_t*)calloc(PW * PH, 1);
    // calloc allocates PW*PH bytes AND automatically zero-initializes every one of them
    // this automatic zero-fill IS the "padding" — no extra code needed to create the zero border
    // (uint8_t*): casts the generic void* that calloc returns into a usable pixel-array pointer

    // Copy image into center of padded buffer
    for (int r = 0; r < H; r++)
        // loops over every row of the ORIGINAL image
        memcpy(pad + (r+2)*PW + 2, in + r*W, W);
        // destination: pad + (r+2)*PW + 2
        //   (r+2): shifts down by 2 rows to skip the top padding
        //   *PW:   converts "row number" into "bytes to skip" (each padded row is PW bytes wide)
        //   +2:    shifts right by 2 columns to skip the left padding
        // source: in + r*W → row r of the original image
        // W: number of bytes to copy (one full row width)

    // NO boundary check inside loop → compiler can vectorize
    for (int r = 0; r < H; r++) {
        // r = current row in terms of the ORIGINAL (unpadded) image coordinates
        for (int c = 0; c < W; c++) {
            // c = current column in terms of the ORIGINAL image coordinates

            int32_t acc = 0;
            // same role as in the first function: accumulates pixel*weight sums, wide enough to avoid overflow

            for (int kr = 0; kr < 5; kr++)
                // kr now ranges 0 to 4 (not -2 to 2!) because we're indexing into the PADDED
                // buffer, where original pixel (r,c) actually sits at padded position (r+2, c+2)
                for (int kc = 0; kc < 5; kc++)
                    // kc follows the same 0-to-4 logic as kr, for the horizontal direction

                    acc += (int32_t)pad[(r+kr)*PW + (c+kc)]
                           * GAUSS[kr][kc];
                    // pad[(r+kr)*PW + (c+kc)]: reads directly from the padded buffer —
                    //   NO bounds checking needed here, because every position in the padded
                    //   buffer is guaranteed valid (either a real pixel or a zero-padding pixel)
                    // GAUSS[kr][kc]: no +2 offset needed here since kr/kc already start at 0

            int32_t v = acc / 273;
            // same normalization as before: divide by the kernel's coefficient sum

            out[r * W + c] = v < 0 ? 0 : (v > 255 ? 255 : v);
            // same clamping into [0, 255] as before
        }
    }
    free(pad);
    // releases the padded buffer's memory back to the system
    // forgetting this line would leak memory every single time this function is called
}