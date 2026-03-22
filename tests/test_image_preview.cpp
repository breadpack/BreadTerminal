#include <gtest/gtest.h>
#include "termcore/image_preview.h"
#include "termcore/config.h"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace termcore;

// ---------------------------------------------------------------------------
// isImageFile tests
// ---------------------------------------------------------------------------

TEST(ImagePreview, IsImageFilePng) {
    EXPECT_TRUE(isImageFile("photo.png"));
    EXPECT_TRUE(isImageFile("PHOTO.PNG"));
    EXPECT_TRUE(isImageFile("/path/to/photo.Png"));
}

TEST(ImagePreview, IsImageFileJpeg) {
    EXPECT_TRUE(isImageFile("photo.jpg"));
    EXPECT_TRUE(isImageFile("photo.jpeg"));
    EXPECT_TRUE(isImageFile("PHOTO.JPEG"));
}

TEST(ImagePreview, IsImageFileGif) {
    EXPECT_TRUE(isImageFile("anim.gif"));
    EXPECT_TRUE(isImageFile("ANIM.GIF"));
}

TEST(ImagePreview, IsImageFileBmp) {
    EXPECT_TRUE(isImageFile("image.bmp"));
}

TEST(ImagePreview, IsImageFileWebp) {
    EXPECT_TRUE(isImageFile("photo.webp"));
}

TEST(ImagePreview, IsImageFileSvg) {
    EXPECT_TRUE(isImageFile("logo.svg"));
}

TEST(ImagePreview, IsImageFileIco) {
    EXPECT_TRUE(isImageFile("favicon.ico"));
}

TEST(ImagePreview, IsImageFileTiff) {
    EXPECT_TRUE(isImageFile("scan.tiff"));
}

TEST(ImagePreview, IsImageFileNonImage) {
    EXPECT_FALSE(isImageFile("document.txt"));
    EXPECT_FALSE(isImageFile("program.exe"));
    EXPECT_FALSE(isImageFile("archive.zip"));
    EXPECT_FALSE(isImageFile("data.json"));
    EXPECT_FALSE(isImageFile("noextension"));
    EXPECT_FALSE(isImageFile(""));
}

TEST(ImagePreview, IsImageFilePathWithDots) {
    EXPECT_TRUE(isImageFile("/some/path.dir/file.png"));
    EXPECT_FALSE(isImageFile("/some/path.png/file.txt"));
}

// ---------------------------------------------------------------------------
// Kitty protocol encoding tests
// ---------------------------------------------------------------------------

TEST(ImagePreview, KittyProtocolEmptyData) {
    std::vector<uint8_t> empty;
    EXPECT_TRUE(encodeForKittyProtocol(empty, 0, 0).empty());
    EXPECT_TRUE(encodeForKittyProtocol(empty, 10, 10).empty());
}

TEST(ImagePreview, KittyProtocolSmallImage) {
    // 1x1 red pixel (RGBA)
    std::vector<uint8_t> pixel = {255, 0, 0, 255};
    std::string result = encodeForKittyProtocol(pixel, 1, 1);

    ASSERT_FALSE(result.empty());
    // Must start with ESC _G
    EXPECT_EQ(result.substr(0, 3), "\033_G");
    // Must contain format=32 (RGBA)
    EXPECT_NE(result.find("f=32"), std::string::npos);
    // Must contain dimensions
    EXPECT_NE(result.find("s=1"), std::string::npos);
    EXPECT_NE(result.find("v=1"), std::string::npos);
    // Must contain action=T (transmit+display)
    EXPECT_NE(result.find("a=T"), std::string::npos);
    // Must end with ESC backslash
    EXPECT_EQ(result.substr(result.size() - 2), "\033\\");
}

TEST(ImagePreview, KittyProtocolContainsSemicolon) {
    // The payload separator ';' must be present
    std::vector<uint8_t> pixel = {0, 0, 0, 255};
    std::string result = encodeForKittyProtocol(pixel, 1, 1);
    EXPECT_NE(result.find(';'), std::string::npos);
}

TEST(ImagePreview, KittyProtocolSingleChunk) {
    // Small image should produce m=0 (no more chunks)
    std::vector<uint8_t> pixel = {128, 128, 128, 255};
    std::string result = encodeForKittyProtocol(pixel, 1, 1);
    EXPECT_NE(result.find("m=0"), std::string::npos);
    // Should NOT have m=1
    EXPECT_EQ(result.find("m=1"), std::string::npos);
}

TEST(ImagePreview, KittyProtocolLargeImageChunked) {
    // Create a large enough RGBA buffer to need multiple chunks.
    // 4096 base64 chars = 3072 bytes of raw data, so we need > 3072 bytes.
    int w = 32, h = 32; // 32*32*4 = 4096 bytes > 3072
    std::vector<uint8_t> data(w * h * 4, 128);
    std::string result = encodeForKittyProtocol(data, w, h);

    // Should have at least one m=1 chunk followed by m=0
    EXPECT_NE(result.find("m=1"), std::string::npos);
    // The last chunk should be m=0
    auto last_m0 = result.rfind("m=0");
    auto last_m1 = result.rfind("m=1");
    EXPECT_GT(last_m0, last_m1);
}

// ---------------------------------------------------------------------------
// iTerm2 protocol encoding tests
// ---------------------------------------------------------------------------

TEST(ImagePreview, ITermProtocolEmptyData) {
    std::vector<uint8_t> empty;
    EXPECT_TRUE(encodeForITermProtocol(empty, 0, 0).empty());
}

TEST(ImagePreview, ITermProtocolSmallImage) {
    // 1x1 blue pixel (RGBA)
    std::vector<uint8_t> pixel = {0, 0, 255, 255};
    std::string result = encodeForITermProtocol(pixel, 1, 1);

    ASSERT_FALSE(result.empty());
    // Must start with OSC 1337 ("\033]1337" is 6 bytes: ESC ] 1 3 3 7)
    EXPECT_EQ(result.substr(0, 6), "\033]1337");
    // Must contain inline=1
    EXPECT_NE(result.find("inline=1"), std::string::npos);
    // Must contain width and height
    EXPECT_NE(result.find("width=1px"), std::string::npos);
    EXPECT_NE(result.find("height=1px"), std::string::npos);
    // Must contain the payload separator ':'
    EXPECT_NE(result.find(':'), std::string::npos);
    // Must end with BEL (0x07)
    EXPECT_EQ(result.back(), '\007');
}

TEST(ImagePreview, ITermProtocolBmpHeader) {
    // Verify the BMP portion: after the colon we have base64 data.
    // Decode the first two bytes of the BMP and check for 'BM'.
    std::vector<uint8_t> pixel = {255, 255, 255, 255};
    std::string result = encodeForITermProtocol(pixel, 1, 1);

    auto colon = result.find(':');
    ASSERT_NE(colon, std::string::npos);
    // Base64 of 'BM' starts with 'Qk' (B=0x42, M=0x4D)
    std::string b64_payload = result.substr(colon + 1,
                                            result.size() - colon - 2); // strip BEL
    EXPECT_GE(b64_payload.size(), 2u);
    EXPECT_EQ(b64_payload[0], 'Q');
    EXPECT_EQ(b64_payload[1], 'k');
}

// ---------------------------------------------------------------------------
// Thumbnail generation with in-memory test image
// ---------------------------------------------------------------------------

namespace {

/// Create a minimal valid 2x2 BMP file in memory and write to disk.
/// Returns the path to the temporary file.
std::string createTestBmp(const std::string& dir) {
    // 2x2 32-bit BMP (BGRA, bottom-up)
    const int w = 2, h = 2;
    const int row_bytes = w * 4;
    const int data_size = row_bytes * h;
    const int file_size = 54 + data_size;

    std::vector<uint8_t> bmp(file_size, 0);
    bmp[0] = 'B'; bmp[1] = 'M';
    std::memcpy(&bmp[2], &file_size, 4);
    int offset = 54;
    std::memcpy(&bmp[10], &offset, 4);
    int dib = 40;
    std::memcpy(&bmp[14], &dib, 4);
    std::memcpy(&bmp[18], &w, 4);
    std::memcpy(&bmp[22], &h, 4);
    uint16_t planes = 1;
    std::memcpy(&bmp[26], &planes, 2);
    uint16_t bpp = 32;
    std::memcpy(&bmp[28], &bpp, 2);
    std::memcpy(&bmp[34], &data_size, 4);

    // Fill pixels: red, green, blue, white (bottom-up row order)
    // Row 0 (bottom): red, green
    bmp[54] = 0; bmp[55] = 0; bmp[56] = 255; bmp[57] = 255;   // B G R A
    bmp[58] = 0; bmp[59] = 255; bmp[60] = 0; bmp[61] = 255;
    // Row 1 (top): blue, white
    bmp[62] = 255; bmp[63] = 0; bmp[64] = 0; bmp[65] = 255;
    bmp[66] = 255; bmp[67] = 255; bmp[68] = 255; bmp[69] = 255;

    std::string path = dir + "/test_image.bmp";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
    return path;
}

} // anonymous namespace

TEST(ImagePreview, GenerateThumbnailFromBmp) {
    // Use current directory for temp file
    std::string path = createTestBmp(".");
    ASSERT_FALSE(path.empty());

    auto thumb = generateThumbnail(path, 2, 2);
    EXPECT_FALSE(thumb.empty());
    // 2x2 RGBA = 16 bytes
    EXPECT_EQ(thumb.size(), 16u);

    // Clean up
    std::remove(path.c_str());
}

TEST(ImagePreview, GenerateThumbnailDownscale) {
    std::string path = createTestBmp(".");
    ASSERT_FALSE(path.empty());

    // Request 1x1 thumbnail from 2x2 source
    auto thumb = generateThumbnail(path, 1, 1);
    EXPECT_FALSE(thumb.empty());
    // 1x1 RGBA = 4 bytes
    EXPECT_EQ(thumb.size(), 4u);

    std::remove(path.c_str());
}

TEST(ImagePreview, GenerateThumbnailNonexistent) {
    auto thumb = generateThumbnail("/nonexistent/image.png", 100, 100);
    EXPECT_TRUE(thumb.empty());
}

TEST(ImagePreview, GetImageInfoFromBmp) {
    std::string path = createTestBmp(".");
    auto info = getImageInfo(path);

    EXPECT_EQ(info.width, 2);
    EXPECT_EQ(info.height, 2);
    EXPECT_EQ(info.format, "bmp");
    EXPECT_GT(info.file_size, 0u);
    EXPECT_EQ(info.path, path);

    std::remove(path.c_str());
}

TEST(ImagePreview, GetImageInfoNonexistent) {
    auto info = getImageInfo("/nonexistent/image.png");
    EXPECT_EQ(info.width, 0);
    EXPECT_EQ(info.height, 0);
}

// ---------------------------------------------------------------------------
// Config enable/disable
// ---------------------------------------------------------------------------

TEST(ImagePreview, ConfigDefaultDisabled) {
    Config cfg;
    EXPECT_FALSE(cfg.image_preview);
    EXPECT_EQ(cfg.image_preview_max_height, 10);
}

TEST(ImagePreview, ConfigEnabled) {
    Config cfg;
    cfg.image_preview = true;
    cfg.image_preview_max_height = 5;
    EXPECT_TRUE(cfg.image_preview);
    EXPECT_EQ(cfg.image_preview_max_height, 5);
}
