#include <gtest/gtest.h>
#include "magnitude.h"
#include "direction.h"
#include <cstdint>
#include <cstring>

// ─── MAGNITUDE TESTS ───────────────────────────────────────────

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
    uint8_t mag[4] = {0};
    gradient_magnitude(Gx, Gy, mag, 4, 1);
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(mag[i], 0);
}

TEST(Magnitude, OutputClampedTo255) {
    int16_t Gx[] = {1000, 0};
    int16_t Gy[] = {0,    0};
    uint8_t mag[2] = {0};
    gradient_magnitude(Gx, Gy, mag, 2, 1);
    EXPECT_LE(mag[0], 255);
    EXPECT_GE(mag[0], 0);
}

// ─── DIRECTION TESTS ───────────────────────────────────────────

TEST(Direction, VerticalEdgeIsHorizontalGradient) {
    // Large Gx, zero Gy → direction 0 (0°)
    int16_t Gx[] = {200, 200};
    int16_t Gy[] = {  0,   0};
    uint8_t dir[2] = {0};
    gradient_direction(Gx, Gy, dir, 2, 1);
    for (int i = 0; i < 2; i++)
        EXPECT_EQ(dir[i], 0);
}

TEST(Direction, HorizontalEdgeIsVerticalGradient) {
    // Zero Gx, large Gy → direction 2 (90°)
    int16_t Gx[] = {  0,   0};
    int16_t Gy[] = {200, 200};
    uint8_t dir[2] = {0};
    gradient_direction(Gx, Gy, dir, 2, 1);
    for (int i = 0; i < 2; i++)
        EXPECT_EQ(dir[i], 2);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
