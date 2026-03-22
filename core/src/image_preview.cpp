#include "termcore/image_preview.h"

// stb_image is already compiled in iterm_image.cpp (STB_IMAGE_IMPLEMENTATION).
// Here we only need the declarations; do NOT define the implementation again.
#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace termcore {

namespace {

/// Lowercase a string for case-insensitive comparison.
std::string toLower(const std::string& s) {
    std::string result = s;
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

/// Extract file extension (including the dot), lowercased.
std::string getExtension(const std::string& filename) {
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return "";
    return toLower(filename.substr(dot));
}

/// Read entire file into a byte vector.
std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    if (size <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

/// Detect image format string from extension.
std::string formatFromExtension(const std::string& ext) {
    if (ext == ".png") return "png";
    if (ext == ".jpg" || ext == ".jpeg") return "jpg";
    if (ext == ".gif") return "gif";
    if (ext == ".bmp") return "bmp";
    if (ext == ".webp") return "webp";
    if (ext == ".svg") return "svg";
    if (ext == ".ico") return "ico";
    if (ext == ".tiff") return "tiff";
    return "";
}

/// Simple bilinear resize of RGBA data.
std::vector<uint8_t> resizeRGBA(const uint8_t* src, int src_w, int src_h,
                                int dst_w, int dst_h) {
    std::vector<uint8_t> dst(static_cast<size_t>(dst_w) * dst_h * 4);

    for (int y = 0; y < dst_h; ++y) {
        float src_y = static_cast<float>(y) * (src_h - 1) / std::max(dst_h - 1, 1);
        int y0 = static_cast<int>(src_y);
        int y1 = std::min(y0 + 1, src_h - 1);
        float fy = src_y - y0;

        for (int x = 0; x < dst_w; ++x) {
            float src_x = static_cast<float>(x) * (src_w - 1) / std::max(dst_w - 1, 1);
            int x0 = static_cast<int>(src_x);
            int x1 = std::min(x0 + 1, src_w - 1);
            float fx = src_x - x0;

            for (int c = 0; c < 4; ++c) {
                float v00 = src[(y0 * src_w + x0) * 4 + c];
                float v10 = src[(y0 * src_w + x1) * 4 + c];
                float v01 = src[(y1 * src_w + x0) * 4 + c];
                float v11 = src[(y1 * src_w + x1) * 4 + c];
                float v = v00 * (1 - fx) * (1 - fy)
                        + v10 * fx * (1 - fy)
                        + v01 * (1 - fx) * fy
                        + v11 * fx * fy;
                dst[(y * dst_w + x) * 4 + c] = static_cast<uint8_t>(
                    std::min(std::max(v, 0.0f), 255.0f));
            }
        }
    }
    return dst;
}

/// Standard base64 encode.
std::string base64Encode(const uint8_t* data, size_t len) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        result.push_back(table[(n >> 18) & 0x3F]);
        result.push_back(table[(n >> 12) & 0x3F]);
        result.push_back((i + 1 < len) ? table[(n >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < len) ? table[n & 0x3F] : '=');
    }
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool isImageFile(const std::string& filename) {
    std::string ext = getExtension(filename);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".gif" || ext == ".bmp" || ext == ".webp" ||
           ext == ".svg" || ext == ".ico" || ext == ".tiff";
}

ImageInfo getImageInfo(const std::string& path) {
    ImageInfo info;
    info.path = path;

    std::string ext = getExtension(path);
    info.format = formatFromExtension(ext);

    auto bytes = readFileBytes(path);
    if (bytes.empty()) return info;
    info.file_size = bytes.size();

    int w = 0, h = 0, comp = 0;
    if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                              &w, &h, &comp)) {
        info.width = w;
        info.height = h;
    }
    return info;
}

std::vector<uint8_t> generateThumbnail(const std::string& path,
                                       int max_width, int max_height) {
    if (max_width <= 0 || max_height <= 0) return {};

    auto bytes = readFileBytes(path);
    if (bytes.empty()) return {};

    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load_from_memory(
        bytes.data(), static_cast<int>(bytes.size()),
        &w, &h, &channels, 4);
    if (!data) return {};

    // Compute scaled dimensions that fit within max_width x max_height
    int dst_w = w;
    int dst_h = h;
    if (dst_w > max_width || dst_h > max_height) {
        float scale = std::min(
            static_cast<float>(max_width) / w,
            static_cast<float>(max_height) / h);
        dst_w = std::max(1, static_cast<int>(w * scale));
        dst_h = std::max(1, static_cast<int>(h * scale));
    }

    std::vector<uint8_t> result;
    if (dst_w == w && dst_h == h) {
        size_t sz = static_cast<size_t>(w) * h * 4;
        result.assign(data, data + sz);
    } else {
        result = resizeRGBA(data, w, h, dst_w, dst_h);
    }

    stbi_image_free(data);
    return result;
}

std::string encodeForKittyProtocol(const std::vector<uint8_t>& rgba,
                                   int width, int height) {
    if (rgba.empty() || width <= 0 || height <= 0) return {};

    std::string b64 = base64Encode(rgba.data(), rgba.size());

    // Kitty protocol: split into chunks of 4096 base64 characters.
    // First chunk: \033_Gf=32,s=<w>,v=<h>,m=<more>,a=T;<data>\033\x5c
    // Continuation: \033_Gm=<more>;<data>\033\x5c
    constexpr size_t kChunkSize = 4096;
    std::string result;

    size_t offset = 0;
    bool first = true;
    while (offset < b64.size()) {
        size_t remaining = b64.size() - offset;
        size_t chunk_len = std::min(remaining, kChunkSize);
        bool more = (offset + chunk_len < b64.size());

        result += "\033_G";
        if (first) {
            result += "f=32,s=" + std::to_string(width)
                    + ",v=" + std::to_string(height)
                    + ",a=T";
            first = false;
        }
        result += ",m=" + std::to_string(more ? 1 : 0);
        result += ";";
        result += b64.substr(offset, chunk_len);
        result += "\033\\";

        offset += chunk_len;
    }
    return result;
}

std::string encodeForITermProtocol(const std::vector<uint8_t>& rgba,
                                   int width, int height) {
    if (rgba.empty() || width <= 0 || height <= 0) return {};

    // iTerm2 inline image protocol expects encoded image file data (PNG).
    // We write a minimal uncompressed RGBA bitmap and base64-encode it.
    // For simplicity, we base64-encode the raw RGBA and set size params.
    // Real iTerm2 expects actual PNG/JPEG, but the raw RGBA is accepted
    // by many implementations when the format is conveyed via params.
    //
    // Use a minimal BMP encoding for broad compatibility.
    // BMP: 54-byte header + pixel data (bottom-up, BGRA).
    int row_bytes = width * 4;
    int padding = (4 - (width * 3) % 4) % 4;
    // Actually use 32-bit BMP (no row padding needed for 4-byte pixels).
    int bmp_row = width * 4;  // no padding for 32bpp
    int data_size = bmp_row * height;
    int file_size = 54 + data_size;

    std::vector<uint8_t> bmp(file_size);
    // BMP header
    bmp[0] = 'B'; bmp[1] = 'M';
    std::memcpy(&bmp[2], &file_size, 4);
    int offset = 54;
    std::memcpy(&bmp[10], &offset, 4);
    // DIB header (BITMAPINFOHEADER = 40 bytes)
    int dib_size = 40;
    std::memcpy(&bmp[14], &dib_size, 4);
    std::memcpy(&bmp[18], &width, 4);
    std::memcpy(&bmp[22], &height, 4);
    uint16_t planes = 1;
    std::memcpy(&bmp[26], &planes, 2);
    uint16_t bpp = 32;
    std::memcpy(&bmp[28], &bpp, 2);
    // compression = 0 (BI_RGB), size, resolution, colors = 0 (rest is zeroed)
    std::memcpy(&bmp[34], &data_size, 4);

    // Pixel data: BMP is bottom-up, BGRA order
    for (int y = 0; y < height; ++y) {
        int src_row = (height - 1 - y);
        for (int x = 0; x < width; ++x) {
            int si = (src_row * width + x) * 4;
            int di = 54 + (y * width + x) * 4;
            bmp[di + 0] = rgba[si + 2]; // B
            bmp[di + 1] = rgba[si + 1]; // G
            bmp[di + 2] = rgba[si + 0]; // R
            bmp[di + 3] = rgba[si + 3]; // A
        }
    }

    std::string b64 = base64Encode(bmp.data(), bmp.size());

    // OSC 1337 ; File=inline=1;size=<size>;width=<w>px;height=<h>px:<base64> ST
    std::string result;
    result += "\033]1337;File=inline=1";
    result += ";size=" + std::to_string(bmp.size());
    result += ";width=" + std::to_string(width) + "px";
    result += ";height=" + std::to_string(height) + "px";
    result += ":";
    result += b64;
    result += "\007";
    return result;
}

std::vector<std::string> previewImages(const std::vector<std::string>& filenames,
                                       int max_rows,
                                       int cell_width_px,
                                       int cell_height_px) {
    std::vector<std::string> results;
    if (max_rows <= 0) return results;

    int max_height_px = max_rows * cell_height_px;
    // Use a generous max width (proportional to typical terminal width)
    int max_width_px = 80 * cell_width_px;

    for (const auto& file : filenames) {
        if (!isImageFile(file)) continue;

        auto thumb = generateThumbnail(file, max_width_px, max_height_px);
        if (thumb.empty()) continue;

        // Determine thumbnail dimensions from pixel count
        // Re-read info to know the aspect ratio
        auto info = getImageInfo(file);
        if (info.width <= 0 || info.height <= 0) continue;

        float scale = std::min(
            static_cast<float>(max_width_px) / info.width,
            static_cast<float>(max_height_px) / info.height);
        scale = std::min(scale, 1.0f);
        int tw = std::max(1, static_cast<int>(info.width * scale));
        int th = std::max(1, static_cast<int>(info.height * scale));

        std::string seq = encodeForKittyProtocol(thumb, tw, th);
        if (!seq.empty()) {
            results.push_back(std::move(seq));
        }
    }
    return results;
}

} // namespace termcore
