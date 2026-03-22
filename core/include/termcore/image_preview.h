#ifndef TERMCORE_IMAGE_PREVIEW_H
#define TERMCORE_IMAGE_PREVIEW_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

/// Metadata about an image file.
struct ImageInfo {
    std::string path;
    int width = 0;
    int height = 0;
    std::string format;   // e.g. "png", "jpg", "gif", "bmp", "webp"
    size_t file_size = 0;
};

/// Check if a filename has a recognized image extension.
/// Supported: .png, .jpg, .jpeg, .gif, .bmp, .webp, .svg, .ico, .tiff
bool isImageFile(const std::string& filename);

/// Read image header to obtain dimensions without loading the full pixel data.
/// Returns an ImageInfo with width/height populated (0 on failure).
ImageInfo getImageInfo(const std::string& path);

/// Generate an RGBA thumbnail that fits within max_width x max_height pixels.
/// Returns empty vector on failure.
std::vector<uint8_t> generateThumbnail(const std::string& path,
                                       int max_width, int max_height);

/// Encode RGBA pixel data as a Kitty graphics protocol escape sequence.
std::string encodeForKittyProtocol(const std::vector<uint8_t>& rgba,
                                   int width, int height);

/// Encode RGBA pixel data as an iTerm2 inline image escape sequence.
std::string encodeForITermProtocol(const std::vector<uint8_t>& rgba,
                                   int width, int height);

/// Given a list of filenames, identify images and return inline preview
/// escape sequences for each. Uses Kitty protocol encoding by default.
/// cell_height_px is used to compute pixel height from max_rows.
/// cell_width_px is used to compute proportional pixel width.
std::vector<std::string> previewImages(const std::vector<std::string>& filenames,
                                       int max_rows = 10,
                                       int cell_width_px = 8,
                                       int cell_height_px = 16);

} // namespace termcore

#endif // TERMCORE_IMAGE_PREVIEW_H
