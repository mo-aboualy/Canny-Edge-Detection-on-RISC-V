#include <gtest/gtest.h>
#include "image_io.h"
#include <cstdio>
#include <vector>

static std::string make_raw_file(const std::vector<uint8_t>& pixels,
                                  uint32_t w, uint32_t h) {
    std::string path = "/tmp/test_image.raw";
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(pixels.data(), 1, w * h, f);
    fclose(f);
    return path;
}

TEST(ImageIO, ReadValidFile) {
    std::vector<uint8_t> data(4 * 4, 128);
    std::string path = make_raw_file(data, 4, 4);
    Image img;
    EXPECT_TRUE(image_read(path.c_str(), 4, 4, img));
    EXPECT_EQ(img.width,  4u);
    EXPECT_EQ(img.height, 4u);
    EXPECT_EQ(img.pixels.size(), 16u);
    EXPECT_EQ(img.pixels[0], 128);
}

TEST(ImageIO, ReadNonExistentFile) {
    Image img;
    EXPECT_FALSE(image_read("/tmp/does_not_exist.raw", 4, 4, img));
}

TEST(ImageIO, ReadWrongSize) {
    std::vector<uint8_t> data(8, 0);
    std::string path = "/tmp/test_wrong.raw";
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(data.data(), 1, 8, f);
    fclose(f);
    Image img;
    EXPECT_FALSE(image_read(path.c_str(), 4, 4, img));
}

TEST(ImageIO, ReadPixelValuesCorrect) {
    std::vector<uint8_t> data = {0, 64, 128, 255};
    std::string path = make_raw_file(data, 2, 2);
    Image img;
    ASSERT_TRUE(image_read(path.c_str(), 2, 2, img));
    EXPECT_EQ(img.pixels[0],   0);
    EXPECT_EQ(img.pixels[1],  64);
    EXPECT_EQ(img.pixels[2], 128);
    EXPECT_EQ(img.pixels[3], 255);
}

TEST(ImageIO, WriteAndReadBack) {
    Image img;
    img.width  = 3;
    img.height = 3;
    img.pixels = {10,20,30, 40,50,60, 70,80,90};
    std::string path = "/tmp/test_write.raw";
    EXPECT_TRUE(image_write(path.c_str(), img));
    Image img2;
    ASSERT_TRUE(image_read(path.c_str(), 3, 3, img2));
    EXPECT_EQ(img.pixels, img2.pixels);
}

TEST(ImageIO, WriteToInvalidPath) {
    Image img;
    img.width = 2; img.height = 2;
    img.pixels = {1, 2, 3, 4};
    EXPECT_FALSE(image_write("/no_such_dir/out.raw", img));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
