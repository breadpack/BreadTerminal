#ifndef TERMCORE_ITERM_IMAGE_H
#define TERMCORE_ITERM_IMAGE_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

/// Parsed dimension specification from iTerm2 inline image protocol.
/// Supports: auto, N (cells), Npx (pixels), N% (percent of terminal).
struct ITermDimension {
    enum class Unit { Auto, Cells, Pixels, Percent };
    Unit unit = Unit::Auto;
    int value = 0;
};

/// Parsed parameters from an OSC 1337 File= sequence.
struct ITermImageParams {
    std::string name;             // base64-decoded filename (optional)
    int size = 0;                 // byte size hint (optional)
    ITermDimension width;         // display width
    ITermDimension height;        // display height
    bool preserve_aspect_ratio = true;
    bool inline_display = false;  // true = display inline, false = download
};

/// A decoded iTerm2 inline image ready for rendering.
struct ITermImage {
    int width = 0;                // pixel width
    int height = 0;               // pixel height
    std::vector<uint8_t> pixels;  // RGBA pixel data
    int display_cols = 0;         // display size in cells
    int display_rows = 0;         // display size in cells
};

/// Parse the OSC 1337 string (everything after "File=").
/// Returns the parsed parameters and separates out the base64 payload.
/// The OSC string format is: key=value;key=value;...:base64data
/// Returns true on success.
bool parseITermImageOsc(const std::string& osc_after_file,
                        ITermImageParams& params,
                        std::string& base64_payload);

/// Decode base64 data into raw bytes.
std::vector<uint8_t> iTermBase64Decode(const std::string& input);

/// Decode image data (PNG, JPEG, GIF, BMP) into RGBA pixels using stb_image.
/// Returns true on success, populating width, height, and pixels.
bool decodeImageData(const std::vector<uint8_t>& encoded_data,
                     int& width, int& height,
                     std::vector<uint8_t>& rgba_pixels);

/// Calculate display cell dimensions from iTerm2 dimension specs.
/// cell_width_px / cell_height_px: terminal cell size in pixels
/// terminal_cols / terminal_rows: terminal grid size
/// image_width_px / image_height_px: natural image dimensions
void calculateDisplayCells(const ITermImageParams& params,
                           int cell_width_px, int cell_height_px,
                           int terminal_cols, int terminal_rows,
                           int image_width_px, int image_height_px,
                           int& out_cols, int& out_rows);

} // namespace termcore

#endif // TERMCORE_ITERM_IMAGE_H
