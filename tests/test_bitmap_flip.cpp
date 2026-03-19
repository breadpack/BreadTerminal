#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace {
void flipBitmapRows(uint8_t* data, int width, int height, int bytesPerPixel) {
    int rowBytes = width * bytesPerPixel;
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = data + y * rowBytes;
        uint8_t* bot = data + (height - 1 - y) * rowBytes;
        for (int i = 0; i < rowBytes; ++i) {
            std::swap(top[i], bot[i]);
        }
    }
}
}

TEST(BitmapFlip, FlipsGrayscaleCorrectly) {
    std::vector<uint8_t> bitmap = {1,2,3, 4,5,6, 7,8,9};
    flipBitmapRows(bitmap.data(), 3, 3, 1);
    std::vector<uint8_t> expected = {7,8,9, 4,5,6, 1,2,3};
    EXPECT_EQ(bitmap, expected);
}

TEST(BitmapFlip, FlipsBGRACorrectly) {
    std::vector<uint8_t> bitmap = {
        10,20,30,40, 50,60,70,80,
        90,100,110,120, 130,140,150,160
    };
    flipBitmapRows(bitmap.data(), 2, 2, 4);
    std::vector<uint8_t> expected = {
        90,100,110,120, 130,140,150,160,
        10,20,30,40, 50,60,70,80
    };
    EXPECT_EQ(bitmap, expected);
}

TEST(BitmapFlip, SingleRowNoOp) {
    std::vector<uint8_t> bitmap = {1,2,3};
    std::vector<uint8_t> original = bitmap;
    flipBitmapRows(bitmap.data(), 3, 1, 1);
    EXPECT_EQ(bitmap, original);
}

TEST(BitmapFlip, EvenHeightFlip) {
    std::vector<uint8_t> bitmap = {1,2, 3,4, 5,6, 7,8};
    flipBitmapRows(bitmap.data(), 2, 4, 1);
    std::vector<uint8_t> expected = {7,8, 5,6, 3,4, 1,2};
    EXPECT_EQ(bitmap, expected);
}
