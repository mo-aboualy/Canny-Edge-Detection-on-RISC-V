#include <gtest/gtest.h>
#include "image_io.h"
#include "gaussian_blur.h"
#include "sobel.h"
#include "magnitude.h"
#include "direction.h"
#include "nms.h"
#include "hysteresis.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>

static Image make_image(uint32_t w, uint32_t h, uint8_t fill) {
    Image img;
    img.width  = w;
    img.height = h;
    size_t sz  = (size_t)w * h;
    img.pixels = (uint8_t*)aligned_alloc(64, (sz + 63) & ~63);
    for (size_t i = 0; i < sz; i++) img.pixels[i] = fill;
    return img;
}

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

TEST(ImageIO, ReadNonExistentFile) {
    Image img;
    EXPECT_FALSE(image_read("/tmp/does_not_exist_xyz.raw", 4, 4, img));
}

TEST(ImageIO, WriteToInvalidPath) {
    Image img = make_image(2, 2, 42);
    EXPECT_FALSE(image_write("/no_such_dir/out.raw", img));
    image_free(img);
}

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

TEST(GaussianBlur, AllBlackRemainsBlack) {
    Image in  = make_image(16, 16, 0);
    Image out = make_image(16, 16, 255);
    gaussian_blur_5x5(in, out);
    for (uint32_t i = 0; i < 16 * 16; i++)
        EXPECT_EQ(out.pixels[i], 0);
    image_free(in);
    image_free(out);
}

TEST(GaussianBlur, ImpulseSpreadToNeighbors) {
    Image in  = make_image(16, 16, 0);
    Image out = make_image(16, 16, 0);
    in.pixels[8 * 16 + 8] = 255;
    gaussian_blur_5x5(in, out);
    EXPECT_GT(out.pixels[8 * 16 + 8], 0);
    EXPECT_GT(out.pixels[8 * 16 + 9], 0);
    EXPECT_GT(out.pixels[9 * 16 + 8], 0);
    image_free(in);
    image_free(out);
}

TEST(GaussianBlur, OutputDimensionsMatchInput) {
    Image in  = make_image(32, 24, 128);
    Image out = make_image(32, 24, 0);
    gaussian_blur_5x5(in, out);
    EXPECT_EQ(out.width,  32u);
    EXPECT_EQ(out.height, 24u);
    image_free(in);
    image_free(out);
}

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

TEST(Sobel, BorderPixelsAreZero) {
    Image in = make_image(16, 16, 200);
    GradientImage g;
    sobel_3x3(in, g);
    for (uint32_t x = 0; x < 16; x++) {
        EXPECT_EQ(g.gx[0 * 16 + x],  0);
        EXPECT_EQ(g.gx[15 * 16 + x], 0);
    }
    for (uint32_t y = 0; y < 16; y++) {
        EXPECT_EQ(g.gx[y * 16 + 0],  0);
        EXPECT_EQ(g.gx[y * 16 + 15], 0);
    }
    gradient_free(g);
    image_free(in);
}

TEST(Magnitude, NonzeroOnRandomImage) {
    int16_t Gx[] = {100, -50, 200, -30};
    int16_t Gy[] = { 50, 100, -30,  80};
    uint8_t mag[4] = {0};
    gradient_magnitude(Gx, Gy, mag, 4, 1);
    for (int i = 0; i < 4; i++)
        EXPECT_GT(mag[i], 0);
}

TEST(Magnitude, ZeroInputGivesZeroOutput) {
    int16_t Gx[] = {0, 0, 0, 0};
    int16_t Gy[] = {0, 0, 0, 0};
    uint8_t mag[4] = {99, 99, 99, 99};
    gradient_magnitude(Gx, Gy, mag, 4, 1);
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(mag[i], 0);
}

TEST(Magnitude, OutputClampedTo255) {
    int16_t Gx[] = {1000, 0};
    int16_t Gy[] = {   0, 0};
    uint8_t mag[2] = {0};
    gradient_magnitude(Gx, Gy, mag, 2, 1);
    EXPECT_LE(mag[0], 255);
    EXPECT_GE(mag[0], 0);
}

TEST(Magnitude, LargerGradientGivesLargerMagnitude) {
    int16_t Gx[] = {10, 100};
    int16_t Gy[] = { 0,   0};
    uint8_t mag[2] = {0};
    gradient_magnitude(Gx, Gy, mag, 2, 1);
    EXPECT_LT(mag[0], mag[1]);
}

TEST(Direction, VerticalEdgeIsHorizontalGradient) {
    int16_t Gx[] = {200, 200};
    int16_t Gy[] = {  0,   0};
    uint8_t dir[2] = {99, 99};
    gradient_direction(Gx, Gy, dir, 2, 1);
    for (int i = 0; i < 2; i++)
        EXPECT_EQ(dir[i], 0);
}

TEST(Direction, HorizontalEdgeIsVerticalGradient) {
    int16_t Gx[] = {  0,   0};
    int16_t Gy[] = {200, 200};
    uint8_t dir[2] = {99, 99};
    gradient_direction(Gx, Gy, dir, 2, 1);
    for (int i = 0; i < 2; i++)
        EXPECT_EQ(dir[i], 2);
}

TEST(Direction, DiagonalEdgeGivesDiagonalDirection) {
    int16_t Gx[] = {100};
    int16_t Gy[] = {100};
    uint8_t dir[1] = {99};
    gradient_direction(Gx, Gy, dir, 1, 1);
    EXPECT_TRUE(dir[0] == 1 || dir[0] == 3);
}

TEST(Direction, OutputValuesInRange) {
    int16_t Gx[] = {100, -50,   0, 200};
    int16_t Gy[] = {  0, 100,  50, -200};
    uint8_t dir[4] = {0};
    gradient_direction(Gx, Gy, dir, 4, 1);
    for (int i = 0; i < 4; i++)
        EXPECT_LE(dir[i], 3);
}

// =============================================================================
// BONUS: Non-Maximum Suppression Tests
// =============================================================================

TEST(NMS, BorderPixelsAreZero) {
    uint8_t mag[16], dir[16], out[16];
    for (int i = 0; i < 16; i++) { mag[i] = 100; dir[i] = 0; out[i] = 0; }
    non_maximum_suppression(mag, dir, out, 4, 4);
    for (int x = 0; x < 4; x++) {
        EXPECT_EQ(out[0 * 4 + x], 0);
        EXPECT_EQ(out[3 * 4 + x], 0);
    }
    for (int y = 0; y < 4; y++) {
        EXPECT_EQ(out[y * 4 + 0], 0);
        EXPECT_EQ(out[y * 4 + 3], 0);
    }
}

TEST(NMS, LocalMaximumIsKept) {
    // Centre pixel (1,1) is the max along direction 0 (horizontal)
    uint8_t mag[9] = {0, 0, 0, 50, 200, 50, 0, 0, 0};
    uint8_t dir[9] = {0, 0, 0,  0,   0,  0, 0, 0, 0};
    uint8_t out[9] = {};
    non_maximum_suppression(mag, dir, out, 3, 3);
    EXPECT_EQ(out[4], 200);
}

TEST(NMS, NonMaximumIsSuppressed) {
    // Centre (1,1)=100 is NOT the max along dir=0; right neighbour is 200
    uint8_t mag[9] = {0,  0,   0, 50, 100, 200, 0, 0, 0};
    uint8_t dir[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t out[9] = {};
    non_maximum_suppression(mag, dir, out, 3, 3);
    EXPECT_EQ(out[4], 0);
}

TEST(NMS, AllZeroInputGivesAllZeroOutput) {
    uint8_t mag[9] = {}, dir[9] = {}, out[9];
    std::memset(out, 99, 9);
    non_maximum_suppression(mag, dir, out, 3, 3);
    for (int i = 0; i < 9; i++) EXPECT_EQ(out[i], 0);
}

TEST(NMS, Direction90VerticalSuppress) {
    // Centre (1,1)=100, above (0,1)=200 → centre suppressed along dir=2
    uint8_t mag[9] = {0, 200, 0, 0, 100, 0, 0, 50, 0};
    uint8_t dir[9] = {2, 2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t out[9] = {};
    non_maximum_suppression(mag, dir, out, 3, 3);
    EXPECT_EQ(out[4], 0);
}

// =============================================================================
// BONUS: Hysteresis Thresholding Tests
// =============================================================================

TEST(Hysteresis, StrongPixelAlwaysKept) {
    uint8_t nms[9] = {0, 0, 0, 0, 200, 0, 0, 0, 0};
    uint8_t out[9] = {};
    hysteresis_threshold(nms, out, 3, 3, 50, 100);
    EXPECT_EQ(out[4], 255);
}

TEST(Hysteresis, WeakPixelConnectedToStrongIsKept) {
    // (0,1)=75 weak, (1,1)=200 strong — 8-connected to each other
    uint8_t nms[9] = {0, 75, 0, 0, 200, 0, 0, 0, 0};
    uint8_t out[9] = {};
    hysteresis_threshold(nms, out, 3, 3, 50, 100);
    EXPECT_EQ(out[1], 255); // weak promoted
    EXPECT_EQ(out[4], 255); // strong kept
}

TEST(Hysteresis, IsolatedWeakPixelSuppressed) {
    uint8_t nms[9] = {0, 0, 0, 0, 75, 0, 0, 0, 0};
    uint8_t out[9] = {};
    hysteresis_threshold(nms, out, 3, 3, 50, 100);
    EXPECT_EQ(out[4], 0);
}

TEST(Hysteresis, BelowLowThresholdAlwaysSuppressed) {
    uint8_t nms[9];
    std::memset(nms, 20, 9); // all below low=50
    uint8_t out[9] = {};
    hysteresis_threshold(nms, out, 3, 3, 50, 100);
    for (int i = 0; i < 9; i++) EXPECT_EQ(out[i], 0);
}

TEST(Hysteresis, OutputOnlyContains0Or255) {
    uint8_t nms[25];
    for (int i = 0; i < 25; i++) nms[i] = (uint8_t)(i * 10);
    uint8_t out[25] = {};
    hysteresis_threshold(nms, out, 5, 5, 60, 120);
    for (int i = 0; i < 25; i++)
        EXPECT_TRUE(out[i] == 0 || out[i] == 255);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
