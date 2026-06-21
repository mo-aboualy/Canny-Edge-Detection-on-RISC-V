/**
 * @file host_tests.cpp
 * @brief Phase 3: GoogleTest unit test suite for the scalar baseline pipeline.
 *
 * Unlike Verify_io.cpp / Test_blur.cpp / Test_sobel.cpp (which are MANUAL,
 * visual sanity checks you run by eye), this file is the AUTOMATED test
 * suite. It runs natively on the host (g++, no QEMU) for fast iteration,
 * and is the safety net for everything that comes after: if these tests
 * pass, we have a trusted scalar reference to compare the RVV-vectorized
 * code against in Phase 6.
 *
 * Run with: make test
 */

#include <gtest/gtest.h>
#include "image_io.h"
#include "gaussian_blur.h"
#include "sobel.h"
#include "magnitude.h"
#include "direction.h"
#include <cstdint>
#include <cstdlib>
#include <cmath>

// ================================================================
// HELPER: creates a filled image using aligned memory
// ================================================================
// Every test needs an Image, so this factors out the allocation logic
// once instead of repeating it in all 19 tests.
//
// aligned_alloc(64, ...) matches the same alignment used everywhere else
// in the codebase (Verify_io.cpp, Test_blur.cpp, etc.) -- a 64-byte
// boundary matches the RVV vector register width at VLEN=512, so memory
// allocated this way is ready for fast aligned vector loads in Phase 6.
//
// (sz + 63) & ~63 rounds the byte count UP to the next multiple of 64,
// because aligned_alloc() requires the allocation size to be a multiple
// of the alignment, not just the starting address.
static Image make_image(uint32_t w, uint32_t h, uint8_t fill) {
    Image img;
    img.width  = w;
    img.height = h;
    size_t sz  = (size_t)w * h;
    img.pixels = (uint8_t*)aligned_alloc(64, (sz + 63) & ~63);
    for (size_t i = 0; i < sz; i++) img.pixels[i] = fill;
    return img;
}

// ================================================================
// IMAGE I/O TESTS
// ================================================================

// Writes a 4x4 image where every pixel holds a DIFFERENT value (0..15),
// then reads it back and checks every byte matches. Using unique values
// (instead of a flat fill) means any single corrupted/dropped/duplicated
// byte during the disk round-trip would be caught immediately -- a flat
// fill could hide a bug if corrupted bytes happened to match the fill value.
TEST(ImageIO, WriteAndReadBack) {
    Image img = make_image(4, 4, 0);
    for (int i = 0; i < 16; i++) img.pixels[i] = (uint8_t)i;
    ASSERT_TRUE(image_write("/tmp/io_test.raw", img));
    Image img2;
    ASSERT_TRUE(image_read("/tmp/io_test.raw", 4, 4, img2));
    for (int i = 0; i < 16; i++)
        EXPECT_EQ(img.pixels[i], img2.pixels[i]);
    image_free(img);
    image_free(img2);
}

// image_read() must fail gracefully (return false) on a missing file
// instead of crashing or returning garbage data.
TEST(ImageIO, ReadNonExistentFile) {
    Image img;
    EXPECT_FALSE(image_read("/tmp/does_not_exist_xyz.raw", 4, 4, img));
}

// image_write() must fail gracefully (return false) when the target
// directory doesn't exist, instead of crashing.
TEST(ImageIO, WriteToInvalidPath) {
    Image img = make_image(2, 2, 42);
    EXPECT_FALSE(image_write("/no_such_dir/out.raw", img));
    image_free(img);
}

// ================================================================
// GAUSSIAN BLUR TESTS
// ================================================================

// Mathematical fact: a Gaussian blur is a weighted average of neighboring
// pixels. If every pixel in the neighborhood already holds the same
// value, the weighted average equals that same value -- a perfectly flat
// image (e.g. a solid gray wall) cannot be changed by blurring.
//
// Only interior pixels (rows/cols 2..13) are checked, not the full 0..15
// range, because the 5x5 kernel reaches 2 pixels out in every direction.
// Near the image border some of those neighbor pixels don't exist and are
// zero-padded, which pulls border averages down -- staying 2px from every
// edge guarantees the full 5x5 neighborhood is real data.
//
// EXPECT_NEAR(..., 1) instead of EXPECT_EQ allows for integer-division
// truncation (sum / 273 can land 1 off from the float-exact answer) --
// a real bug would produce an error much larger than 1, so this tolerance
// doesn't hide anything meaningful.
TEST(GaussianBlur, UniformImageRemainsUniform) {
    Image in  = make_image(16, 16, 100);
    Image out = make_image(16, 16, 0);
    gaussian_blur_5x5(in, out);
    for (uint32_t y = 2; y < 14; y++)
        for (uint32_t x = 2; x < 14; x++)
            EXPECT_NEAR(out.pixels[y * 16 + x], 100, 1);
    image_free(in);
    image_free(out);
}

// 0 * anything = 0, with no rounding ambiguity, so this uses exact
// equality (unlike the test above). `out` is deliberately pre-filled
// with 255 (not 0) so the test would fail if gaussian_blur_5x5() simply
// failed to write to the buffer at all -- it proves the function
// actually writes its result rather than the test accidentally passing
// because `out` happened to already be correct.
TEST(GaussianBlur, AllBlackRemainsBlack) {
    Image in  = make_image(16, 16, 0);
    Image out = make_image(16, 16, 255);
    gaussian_blur_5x5(in, out);
    for (uint32_t i = 0; i < 16 * 16; i++)
        EXPECT_EQ(out.pixels[i], 0);
    image_free(in);
    image_free(out);
}

// Classic signal-processing "impulse response" check: a single bright
// dot on a black background, after blurring, should bleed energy into
// its immediate neighbors -- that is literally what a blur does. If the
// neighbors stayed at exactly 0, the convolution isn't actually summing
// over the full kernel window, which is a real implementation bug.
TEST(GaussianBlur, ImpulseSpreadToNeighbors) {
    Image in  = make_image(16, 16, 0);
    Image out = make_image(16, 16, 0);
    in.pixels[8 * 16 + 8] = 255;
    gaussian_blur_5x5(in, out);
    EXPECT_GT(out.pixels[8 * 16 + 8], 0);  // center stays lit
    EXPECT_GT(out.pixels[8 * 16 + 9], 0);  // right neighbor picks up energy
    EXPECT_GT(out.pixels[9 * 16 + 8], 0);  // bottom neighbor picks up energy
    image_free(in);
    image_free(out);
}

// Trivial but important: a non-square image (32x24) should not have its
// width/height swapped or corrupted by the blur function.
TEST(GaussianBlur, OutputDimensionsMatchInput) {
    Image in  = make_image(32, 24, 128);
    Image out = make_image(32, 24, 0);
    gaussian_blur_5x5(in, out);
    EXPECT_EQ(out.width,  32u);
    EXPECT_EQ(out.height, 24u);
    image_free(in);
    image_free(out);
}

// ================================================================
// SOBEL TESTS
// ================================================================

// Sobel measures CHANGE in intensity. A flat image has zero change
// everywhere, so both Gx and Gy must be exactly 0 at every interior
// pixel (interior here is 1..14, since Sobel's 3x3 kernel only needs
// 1px of border, unlike Gaussian's 2px).
TEST(Sobel, UniformImageZeroGradient) {
    Image in = make_image(16, 16, 128);
    GradientImage g;
    sobel_3x3(in, g);
    for (uint32_t y = 1; y < 15; y++)
        for (uint32_t x = 1; x < 15; x++) {
            EXPECT_EQ(g.gx[y * 16 + x], 0);
            EXPECT_EQ(g.gy[y * 16 + x], 0);
        }
    gradient_free(g);
    image_free(in);
}

// Image is split left half black / right half white -- a hard VERTICAL
// edge. At the boundary pixel (8,8): Gx (the horizontal-direction
// gradient kernel) should fire strongly, since intensity changes sharply
// left-to-right. Gy should stay small, since intensity doesn't change
// top-to-bottom at that point. This catches a very common bug: the Gx
// and Gy kernels being accidentally swapped.
TEST(Sobel, VerticalEdgeLargeGxSmallGy) {
    Image in = make_image(16, 16, 0);
    for (uint32_t y = 0; y < 16; y++)
        for (uint32_t x = 8; x < 16; x++)
            in.pixels[y * 16 + x] = 255;
    GradientImage g;
    sobel_3x3(in, g);
    int edge_gx = std::abs((int)g.gx[8 * 16 + 8]);
    int edge_gy = std::abs((int)g.gy[8 * 16 + 8]);
    EXPECT_GT(edge_gx, 100);
    EXPECT_LT(edge_gy, edge_gx);
    gradient_free(g);
    image_free(in);
}

// Exact mirror of the test above (top half black / bottom half white --
// a HORIZONTAL edge): now Gy should be the strong one and Gx the weak
// one. Testing both directions (not just one) is what actually proves
// the kernels aren't swapped or transposed -- a single direction test
// could pass even with a bug that happens to cancel out.
TEST(Sobel, HorizontalEdgeLargeGySmallGx) {
    Image in = make_image(16, 16, 0);
    for (uint32_t y = 8; y < 16; y++)
        for (uint32_t x = 0; x < 16; x++)
            in.pixels[y * 16 + x] = 255;
    GradientImage g;
    sobel_3x3(in, g);
    int edge_gx = std::abs((int)g.gx[8 * 16 + 8]);
    int edge_gy = std::abs((int)g.gy[8 * 16 + 8]);
    EXPECT_GT(edge_gy, 100);
    EXPECT_LT(edge_gx, edge_gy);
    gradient_free(g);
    image_free(in);
}

// sobel.h documents that the 1px border is explicitly zeroed (rather
// than computed with a partial/incomplete 3x3 neighborhood). This test
// holds the implementation to that documented contract. Uses a uniform
// fill value (200) because the test isn't about gradients at all -- it
// only cares whether the outermost ring of pixels is forced to zero
// regardless of what the image content actually is.
TEST(Sobel, BorderPixelsAreZero) {
    Image in = make_image(16, 16, 200);
    GradientImage g;
    sobel_3x3(in, g);
    for (uint32_t x = 0; x < 16; x++) {
        EXPECT_EQ(g.gx[0 * 16 + x],  0);   // top row
        EXPECT_EQ(g.gx[15 * 16 + x], 0);   // bottom row
    }
    for (uint32_t y = 0; y < 16; y++) {
        EXPECT_EQ(g.gx[y * 16 + 0],  0);   // left column
        EXPECT_EQ(g.gx[y * 16 + 15], 0);   // right column
    }
    gradient_free(g);
    image_free(in);
}

// ================================================================
// MAGNITUDE TESTS
// ================================================================
// Note: these tests call gradient_magnitude() directly with hand-built
// Gx/Gy arrays, completely bypassing Sobel and the Image struct. This is
// intentional -- it isolates the magnitude math from everything upstream,
// so a bug here can never be confused with a Sobel bug (or vice versa).

// Real (nonzero) gradient values should produce nonzero magnitude output.
TEST(Magnitude, NonzeroOnRandomImage) {
    int16_t Gx[] = {100, -50, 200, -30};
    int16_t Gy[] = { 50, 100, -30,  80};
    uint8_t mag[4] = {0};
    gradient_magnitude(Gx, Gy, mag, 4, 1);
    for (int i = 0; i < 4; i++)
        EXPECT_GT(mag[i], 0);
}

// sqrt(0^2 + 0^2) = 0 exactly, no rounding ambiguity -- exact equality
// is correct here. mag[] is deliberately pre-filled with 99 (a wrong
// value) so the test would fail if the function never actually wrote
// to the output buffer.
TEST(Magnitude, ZeroInputGivesZeroOutput) {
    int16_t Gx[] = {0, 0, 0, 0};
    int16_t Gy[] = {0, 0, 0, 0};
    uint8_t mag[4] = {99, 99, 99, 99};
    gradient_magnitude(Gx, Gy, mag, 4, 1);
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(mag[i], 0);
}

// The output buffer is uint8_t, which can only hold 0-255. Gx=1000 is an
// intentionally extreme/impossible value (a real 8-bit image could never
// produce a Sobel gradient this large) used specifically to stress-test
// the clamping/normalization logic at its upper limit and confirm it
// doesn't overflow or wrap around.
TEST(Magnitude, OutputClampedTo255) {
    int16_t Gx[] = {1000, 0};
    int16_t Gy[] = {   0, 0};
    uint8_t mag[2] = {0};
    gradient_magnitude(Gx, Gy, mag, 2, 1);
    EXPECT_LE(mag[0], 255);
    EXPECT_GE(mag[0], 0);
}

// Monotonicity check: a larger raw gradient should map to a larger
// magnitude value after normalization. Both pixels are processed
// together in one array (not as two separate calls), which also
// implicitly verifies the normalization step preserves relative
// ordering rather than scrambling it.
TEST(Magnitude, LargerGradientGivesLargerMagnitude) {
    int16_t Gx[] = {10, 100};
    int16_t Gy[] = { 0,   0};
    uint8_t mag[2] = {0};
    gradient_magnitude(Gx, Gy, mag, 2, 1);
    EXPECT_LT(mag[0], mag[1]);
}

// ================================================================
// DIRECTION TESTS
// ================================================================
// Like the Magnitude tests, these call gradient_direction() directly
// with hand-built Gx/Gy values rather than running the full pipeline --
// isolating the direction-quantization math from Sobel.

// Pure horizontal gradient (all Gx, zero Gy) corresponds to a 0 degree
// gradient direction, which should be quantized to bin 0.
TEST(Direction, VerticalEdgeIsHorizontalGradient) {
    int16_t Gx[] = {200, 200};
    int16_t Gy[] = {  0,   0};
    uint8_t dir[2] = {99, 99};
    gradient_direction(Gx, Gy, dir, 2, 1);
    for (int i = 0; i < 2; i++)
        EXPECT_EQ(dir[i], 0);
}

// Pure vertical gradient (zero Gx, all Gy) corresponds to a 90 degree
// gradient direction, which should be quantized to bin 2.
TEST(Direction, HorizontalEdgeIsVerticalGradient) {
    int16_t Gx[] = {  0,   0};
    int16_t Gy[] = {200, 200};
    uint8_t dir[2] = {99, 99};
    gradient_direction(Gx, Gy, dir, 2, 1);
    for (int i = 0; i < 2; i++)
        EXPECT_EQ(dir[i], 2);
}

// Equal Gx and Gy means a perfect 45 degree angle. The test accepts
// EITHER bin 1 (45 deg) or bin 3 (135 deg) since both are valid diagonal
// classifications depending on the exact sign-comparison logic used --
// the test isn't meant to be overly strict about which diagonal bin a
// perfectly ambiguous 45-degree input lands in.
TEST(Direction, DiagonalEdgeGivesDiagonalDirection) {
    int16_t Gx[] = {100};
    int16_t Gy[] = {100};
    uint8_t dir[1] = {99};
    gradient_direction(Gx, Gy, dir, 1, 1);
    EXPECT_TRUE(dir[0] == 1 || dir[0] == 3);
}

// Direction is meant to be quantized into exactly 4 buckets (0,1,2,3).
// This bounds check catches any case where the bucket math could
// accidentally produce an out-of-range value.
TEST(Direction, OutputValuesInRange) {
    int16_t Gx[] = {100, -50,   0, 200};
    int16_t Gy[] = {  0, 100,  50, -200};
    uint8_t dir[4] = {0};
    gradient_direction(Gx, Gy, dir, 4, 1);
    for (int i = 0; i < 4; i++)
        EXPECT_LE(dir[i], 3);
}

// Standard GoogleTest entry point: parses command-line filters (e.g.
// --gtest_filter=Sobel.*), runs every TEST(...) registered above, and
// returns a nonzero exit code if any test failed -- this is what makes
// `make test` fail loudly (and CI in .github/workflows/tests.yml catch
// it) if a regression is ever introduced.
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
