#include "CoreTextRasterizer.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <cmath>
#include <mutex>
#include <unordered_map>

namespace termcore {

namespace {

/// RAII wrapper for Core Foundation objects.
template <typename T>
class CFPtr {
public:
    CFPtr() : ref_(nullptr) {}
    explicit CFPtr(T ref, bool retain = false) : ref_(ref) {
        if (retain && ref_) CFRetain(ref_);
    }
    ~CFPtr() { if (ref_) CFRelease(ref_); }

    CFPtr(const CFPtr& other) : ref_(other.ref_) {
        if (ref_) CFRetain(ref_);
    }
    CFPtr& operator=(const CFPtr& other) {
        if (this != &other) {
            if (ref_) CFRelease(ref_);
            ref_ = other.ref_;
            if (ref_) CFRetain(ref_);
        }
        return *this;
    }

    CFPtr(CFPtr&& other) noexcept : ref_(other.ref_) { other.ref_ = nullptr; }
    CFPtr& operator=(CFPtr&& other) noexcept {
        if (this != &other) {
            if (ref_) CFRelease(ref_);
            ref_ = other.ref_;
            other.ref_ = nullptr;
        }
        return *this;
    }

    T get() const { return ref_; }
    explicit operator bool() const { return ref_ != nullptr; }

private:
    T ref_;
};

/// Stored font data for a loaded face.
struct FontFaceEntry {
    CFPtr<CTFontRef> font;
    float size;
};

void setupContext(CGContextRef ctx) {
    CGContextSetShouldSubpixelPositionFonts(ctx, true);
    CGContextSetShouldSmoothFonts(ctx, true);
    CGContextSetAllowsAntialiasing(ctx, true);
    CGContextSetShouldAntialias(ctx, true);
}

bool checkColorGlyph(CTFontRef font) {
    CTFontSymbolicTraits traits = CTFontGetSymbolicTraits(font);
    return (traits & kCTFontTraitColorGlyphs) != 0;
}

/// Flip bitmap rows in-place: converts CG Y-up to Metal Y-down.
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

} // namespace

class CoreTextRasterizerImpl : public IFontRasterizer {
public:
    CoreTextRasterizerImpl() = default;
    ~CoreTextRasterizerImpl() override = default;

    FontFaceId loadFont(const std::string& path, int face_index, float size) override;
    RasterizedGlyph rasterize(FontFaceId face, uint32_t glyph_index,
                               float size, SubpixelOffset offset) override;
    FontMetrics getMetrics(FontFaceId face, float size) override;
    bool isColorGlyph(FontFaceId face, uint32_t glyph_index) override;
    uint32_t getGlyphIndex(FontFaceId face, char32_t codepoint) override;
    void setScaleFactor(float scale) override { scaleFactor_ = scale; }

private:
    float scaleFactor_ = 2.0f;
    CTFontRef getFontRef(FontFaceId face) const;
    CFPtr<CTFontRef> createFontFromFile(const std::string& path, int face_index, float size);
    CFPtr<CTFontRef> getSizedFont(FontFaceId face, float size);

    std::mutex mutex_;
    FontFaceId next_id_ = 1;
    std::unordered_map<FontFaceId, FontFaceEntry> fonts_;
};

CFPtr<CTFontRef> CoreTextRasterizerImpl::createFontFromFile(
    const std::string& path, int face_index, float size) {

    CFPtr<CFStringRef> cfPath(
        CFStringCreateWithCString(kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8));
    if (!cfPath) return {};

    CFPtr<CFURLRef> url(CFURLCreateWithFileSystemPath(
        kCFAllocatorDefault, cfPath.get(), kCFURLPOSIXPathStyle, false));
    if (!url) return {};

    CFPtr<CFArrayRef> descriptors(CTFontManagerCreateFontDescriptorsFromURL(url.get()));
    if (!descriptors || CFArrayGetCount(descriptors.get()) == 0) return {};

    CFIndex count = CFArrayGetCount(descriptors.get());
    CFIndex index = (face_index >= 0 && face_index < count) ? face_index : 0;

    CTFontDescriptorRef desc =
        (CTFontDescriptorRef)CFArrayGetValueAtIndex(descriptors.get(), index);
    if (!desc) return {};

    CTFontRef font = CTFontCreateWithFontDescriptor(desc, size, nullptr);
    if (!font) return {};

    return CFPtr<CTFontRef>(font);
}

FontFaceId CoreTextRasterizerImpl::loadFont(const std::string& path,
                                             int face_index, float size) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto ctFont = createFontFromFile(path, face_index, size);
    if (!ctFont) return kInvalidFontFace;

    FontFaceId id = next_id_++;
    fonts_[id] = FontFaceEntry{std::move(ctFont), size};
    return id;
}

CTFontRef CoreTextRasterizerImpl::getFontRef(FontFaceId face) const {
    auto it = fonts_.find(face);
    if (it == fonts_.end()) return nullptr;
    return it->second.font.get();
}

CFPtr<CTFontRef> CoreTextRasterizerImpl::getSizedFont(FontFaceId face, float size) {
    auto it = fonts_.find(face);
    if (it == fonts_.end()) return {};

    CTFontRef baseFont = it->second.font.get();
    if (std::abs(it->second.size - size) <= 0.01f) {
        return CFPtr<CTFontRef>(baseFont, /*retain=*/true);
    }

    CTFontRef newFont = CTFontCreateCopyWithAttributes(baseFont, size, nullptr, nullptr);
    if (!newFont) return CFPtr<CTFontRef>(baseFont, /*retain=*/true);
    return CFPtr<CTFontRef>(newFont);
}

RasterizedGlyph CoreTextRasterizerImpl::rasterize(FontFaceId face,
                                                    uint32_t glyph_index,
                                                    float size,
                                                    SubpixelOffset offset) {
    RasterizedGlyph result;
    std::lock_guard<std::mutex> lock(mutex_);

    auto fontPtr = getSizedFont(face, size);
    CTFontRef font = fontPtr.get();
    if (!font) return result;

    CGGlyph glyphId = static_cast<CGGlyph>(glyph_index);
    bool color = checkColorGlyph(font);

    CGRect bbox;
    CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationDefault, &glyphId, &bbox, 1);

    float scale = scaleFactor_;

    float subX = static_cast<float>(offset.x) * 0.25f;
    float subY = static_cast<float>(offset.y) * 0.25f;

    // Bitmap dimensions in physical pixels
    // Must include the full bounding box (origin can be negative for descenders)
    // +2px padding for texture sampling safety
    float bboxLeft = bbox.origin.x;                    // Can be negative
    float bboxBottom = bbox.origin.y;                  // Negative for descenders
    float bboxRight = bbox.origin.x + bbox.size.width;
    float bboxTop = bbox.origin.y + bbox.size.height;

    int32_t w = static_cast<int32_t>(std::ceil((bboxRight - std::min(bboxLeft, 0.0f)) * scale)) + 2;
    int32_t h = static_cast<int32_t>(std::ceil((bboxTop - std::min(bboxBottom, 0.0f)) * scale)) + 2;
    w = std::max(w, 1);
    h = std::max(h, 1);

    // Bearings in physical pixels (consistent with draw position)
    // bearing_x: pen origin to glyph left edge (can be negative for overhangs)
    result.bearing_x = static_cast<int32_t>(std::round(bboxLeft * scale));
    // bearing_y: baseline to glyph TOP edge (always positive for above-baseline)
    result.bearing_y = static_cast<int32_t>(std::round(bboxTop * scale));

    if (color) {
        result.format = PixelFormat::BGRA;
        size_t bytesPerRow = static_cast<size_t>(w) * 4;
        result.bitmap.resize(bytesPerRow * static_cast<size_t>(h), 0);

        CFPtr<CGColorSpaceRef> colorSpace(CGColorSpaceCreateDeviceRGB());
        CFPtr<CGContextRef> ctx(CGBitmapContextCreate(
            result.bitmap.data(), w, h, 8, bytesPerRow, colorSpace.get(),
            static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little));
        if (!ctx) return RasterizedGlyph{};

        setupContext(ctx.get());
        // Scale CTM so CoreText draws at 2x into the pixel-sized bitmap
        CGContextScaleCTM(ctx.get(), scale, scale);
        // Draw position: offset so glyph fits in bitmap (points, CTM scales to pixels)
        float drawX = -std::min(bboxLeft, 0.0f) + subX;
        float drawY = -std::min(bboxBottom, 0.0f) + subY;
        CGPoint position = CGPointMake(drawX, drawY);
        CTFontDrawGlyphs(font, &glyphId, &position, 1, ctx.get());
        flipBitmapRows(result.bitmap.data(), w, h, 4);
    } else {
        result.format = PixelFormat::Grayscale;
        size_t bytesPerRow = static_cast<size_t>(w) * 4;
        std::vector<uint8_t> rgbaBuf(bytesPerRow * static_cast<size_t>(h), 0);

        CFPtr<CGColorSpaceRef> colorSpace(CGColorSpaceCreateDeviceRGB());
        CFPtr<CGContextRef> ctx(CGBitmapContextCreate(
            rgbaBuf.data(), w, h, 8, bytesPerRow, colorSpace.get(),
            static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little));
        if (!ctx) return RasterizedGlyph{};

        setupContext(ctx.get());
        CGContextSetRGBFillColor(ctx.get(), 1.0, 1.0, 1.0, 1.0);
        // Scale CTM so CoreText draws at 2x into the pixel-sized bitmap
        CGContextScaleCTM(ctx.get(), scale, scale);
        // Draw position: offset so glyph fits in bitmap (points, CTM scales to pixels)
        float drawX = -std::min(bboxLeft, 0.0f) + subX;
        float drawY = -std::min(bboxBottom, 0.0f) + subY;
        CGPoint position = CGPointMake(drawX, drawY);
        CTFontDrawGlyphs(font, &glyphId, &position, 1, ctx.get());
        flipBitmapRows(rgbaBuf.data(), w, h, 4);

        // Extract alpha channel as grayscale
        result.bitmap.resize(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
        for (int32_t y = 0; y < h; ++y) {
            for (int32_t x = 0; x < w; ++x) {
                size_t srcIdx = (static_cast<size_t>(y) * static_cast<size_t>(w) + x) * 4;
                size_t dstIdx = static_cast<size_t>(y) * static_cast<size_t>(w) + x;
                result.bitmap[dstIdx] = rgbaBuf[srcIdx + 3];
            }
        }
    }

    result.width = w;
    result.height = h;
    return result;
}

FontMetrics CoreTextRasterizerImpl::getMetrics(FontFaceId face, float size) {
    FontMetrics metrics{};
    std::lock_guard<std::mutex> lock(mutex_);

    auto fontPtr = getSizedFont(face, size);
    CTFontRef font = fontPtr.get();
    if (!font) return metrics;

    float scale = scaleFactor_;

    // All metrics in physical pixels
    metrics.ascent = static_cast<float>(CTFontGetAscent(font)) * scale;
    metrics.descent = static_cast<float>(CTFontGetDescent(font)) * scale;
    float leading = static_cast<float>(CTFontGetLeading(font)) * scale;
    metrics.cell_height = std::ceil(metrics.ascent + metrics.descent + leading);

    UniChar spaceChar = ' ';
    CGGlyph spaceGlyph;
    if (CTFontGetGlyphsForCharacters(font, &spaceChar, &spaceGlyph, 1)) {
        CGSize advance;
        CTFontGetAdvancesForGlyphs(font, kCTFontOrientationDefault, &spaceGlyph, &advance, 1);
        metrics.cell_width = static_cast<float>(advance.width) * scale;
    } else {
        UniChar mChar = 'M';
        CGGlyph mGlyph;
        if (CTFontGetGlyphsForCharacters(font, &mChar, &mGlyph, 1)) {
            CGSize advance;
            CTFontGetAdvancesForGlyphs(font, kCTFontOrientationDefault, &mGlyph, &advance, 1);
            metrics.cell_width = static_cast<float>(advance.width) * scale;
        }
    }

    metrics.underline_position = static_cast<float>(CTFontGetUnderlinePosition(font)) * scale;
    metrics.underline_thickness = static_cast<float>(CTFontGetUnderlineThickness(font)) * scale;
    metrics.strikethrough_position = metrics.ascent * 0.3f;
    metrics.strikethrough_thickness = metrics.underline_thickness;

    return metrics;
}

bool CoreTextRasterizerImpl::isColorGlyph(FontFaceId face, uint32_t glyph_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    CTFontRef font = getFontRef(face);
    if (!font) return false;
    return checkColorGlyph(font);
}

uint32_t CoreTextRasterizerImpl::getGlyphIndex(FontFaceId face, char32_t codepoint) {
    std::lock_guard<std::mutex> lock(mutex_);
    CTFontRef font = getFontRef(face);
    if (!font) return 0;

    CGGlyph glyphs[2] = {0, 0};
    UniChar characters[2];
    CFIndex charCount = 0;

    if (codepoint <= 0xFFFF) {
        characters[0] = static_cast<UniChar>(codepoint);
        charCount = 1;
    } else {
        char32_t code = codepoint - 0x10000;
        characters[0] = static_cast<UniChar>(0xD800 + (code >> 10));
        characters[1] = static_cast<UniChar>(0xDC00 + (code & 0x3FF));
        charCount = 2;
    }

    if (!CTFontGetGlyphsForCharacters(font, characters, glyphs, charCount)) {
        return 0;
    }
    return glyphs[0];
}

std::unique_ptr<IFontRasterizer> createCoreTextRasterizer() {
    return std::make_unique<CoreTextRasterizerImpl>();
}

} // namespace termcore
