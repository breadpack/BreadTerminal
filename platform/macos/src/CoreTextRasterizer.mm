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

private:
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

    float subX = static_cast<float>(offset.x) * 0.25f;
    float subY = static_cast<float>(offset.y) * 0.25f;

    int32_t w = static_cast<int32_t>(std::ceil(bbox.size.width + std::abs(bbox.origin.x) + 2));
    int32_t h = static_cast<int32_t>(std::ceil(bbox.size.height + std::abs(bbox.origin.y) + 2));
    w = std::max(w, 1);
    h = std::max(h, 1);

    result.bearing_x = static_cast<int32_t>(std::floor(bbox.origin.x));
    result.bearing_y = static_cast<int32_t>(std::ceil(bbox.origin.y + bbox.size.height));

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
        CGPoint position = CGPointMake(-bbox.origin.x + subX, -bbox.origin.y + subY);
        CTFontDrawGlyphs(font, &glyphId, &position, 1, ctx.get());
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

        CGPoint position = CGPointMake(-bbox.origin.x + subX, -bbox.origin.y + subY);
        CTFontDrawGlyphs(font, &glyphId, &position, 1, ctx.get());

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

    metrics.ascent = static_cast<float>(CTFontGetAscent(font));
    metrics.descent = static_cast<float>(CTFontGetDescent(font));
    float leading = static_cast<float>(CTFontGetLeading(font));
    metrics.cell_height = std::ceil(metrics.ascent + metrics.descent + leading);

    UniChar spaceChar = ' ';
    CGGlyph spaceGlyph;
    if (CTFontGetGlyphsForCharacters(font, &spaceChar, &spaceGlyph, 1)) {
        CGSize advance;
        CTFontGetAdvancesForGlyphs(font, kCTFontOrientationDefault, &spaceGlyph, &advance, 1);
        metrics.cell_width = static_cast<float>(advance.width);
    } else {
        UniChar mChar = 'M';
        CGGlyph mGlyph;
        if (CTFontGetGlyphsForCharacters(font, &mChar, &mGlyph, 1)) {
            CGSize advance;
            CTFontGetAdvancesForGlyphs(font, kCTFontOrientationDefault, &mGlyph, &advance, 1);
            metrics.cell_width = static_cast<float>(advance.width);
        }
    }

    metrics.underline_position = static_cast<float>(CTFontGetUnderlinePosition(font));
    metrics.underline_thickness = static_cast<float>(CTFontGetUnderlineThickness(font));
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
