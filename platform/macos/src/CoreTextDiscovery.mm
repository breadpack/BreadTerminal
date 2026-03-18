#include "CoreTextDiscovery.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>

#include <string>
#include <vector>

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

    CFPtr(const CFPtr&) = delete;
    CFPtr& operator=(const CFPtr&) = delete;

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

/// Convert a CFStringRef to std::string. Returns empty string on failure.
std::string cfStringToStd(CFStringRef cfStr) {
    if (!cfStr) return {};

    const char* cStr = CFStringGetCStringPtr(cfStr, kCFStringEncodingUTF8);
    if (cStr) return std::string(cStr);

    CFIndex length = CFStringGetLength(cfStr);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string result(static_cast<size_t>(maxSize), '\0');
    if (CFStringGetCString(cfStr, result.data(), maxSize, kCFStringEncodingUTF8)) {
        result.resize(std::strlen(result.c_str()));
        return result;
    }
    return {};
}

/// Extract file path from a font descriptor's URL attribute.
std::string extractFilePath(CTFontDescriptorRef desc) {
    CFPtr<CFURLRef> url(
        static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute)));
    if (!url) return {};

    CFPtr<CFStringRef> pathStr(CFURLCopyFileSystemPath(url.get(), kCFURLPOSIXPathStyle));
    return cfStringToStd(pathStr.get());
}

/// Extract FontStyle from symbolic traits.
FontStyle extractStyle(CTFontDescriptorRef desc) {
    CFPtr<CFDictionaryRef> traits(
        static_cast<CFDictionaryRef>(CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute)));
    if (!traits) return FontStyle::Regular;

    CTFontSymbolicTraits symbolic = 0;
    CFNumberRef symbolicRef = static_cast<CFNumberRef>(
        CFDictionaryGetValue(traits.get(), kCTFontSymbolicTrait));
    if (symbolicRef) {
        CFNumberGetValue(symbolicRef, kCFNumberSInt32Type, &symbolic);
    }

    bool bold = (symbolic & kCTFontTraitBold) != 0;
    bool italic = (symbolic & kCTFontTraitItalic) != 0;

    if (bold && italic) return FontStyle::BoldItalic;
    if (bold) return FontStyle::Bold;
    if (italic) return FontStyle::Italic;
    return FontStyle::Regular;
}

/// Extract CSS weight (100-900) from font traits.
int extractWeight(CTFontDescriptorRef desc) {
    CFPtr<CFDictionaryRef> traits(
        static_cast<CFDictionaryRef>(CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute)));
    if (!traits) return 400;

    CFNumberRef weightRef = static_cast<CFNumberRef>(
        CFDictionaryGetValue(traits.get(), kCTFontWeightTrait));
    if (!weightRef) return 400;

    double ctWeight = 0.0;
    CFNumberGetValue(weightRef, kCFNumberFloat64Type, &ctWeight);

    // Core Text weight ranges roughly from -1.0 (thin) to 1.0 (heavy).
    // Map to CSS 100-900 scale. CT 0.0 corresponds to CSS 400 (Regular).
    int cssWeight = static_cast<int>(400.0 + ctWeight * 500.0);
    if (cssWeight < 100) cssWeight = 100;
    if (cssWeight > 900) cssWeight = 900;
    return cssWeight;
}

/// Build a FontDescriptor from a CTFontDescriptorRef.
FontDescriptor descriptorFromCT(CTFontDescriptorRef desc) {
    FontDescriptor fd;

    CFPtr<CFStringRef> familyName(
        static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(desc, kCTFontFamilyNameAttribute)));
    fd.family = cfStringToStd(familyName.get());

    CFPtr<CFStringRef> psName(
        static_cast<CFStringRef>(CTFontDescriptorCopyAttribute(desc, kCTFontNameAttribute)));
    fd.postscript_name = cfStringToStd(psName.get());

    fd.file_path = extractFilePath(desc);
    fd.style = extractStyle(desc);
    fd.weight = extractWeight(desc);
    fd.face_index = 0;

    return fd;
}

/// Build symbolic traits mask from FontStyle.
CTFontSymbolicTraits traitsFromStyle(FontStyle style) {
    CTFontSymbolicTraits traits = 0;
    auto s = static_cast<uint8_t>(style);
    if (s & static_cast<uint8_t>(FontStyle::Bold)) traits |= kCTFontTraitBold;
    if (s & static_cast<uint8_t>(FontStyle::Italic)) traits |= kCTFontTraitItalic;
    return traits;
}

} // namespace

class CoreTextDiscoveryImpl : public IFontDiscovery {
public:
    CoreTextDiscoveryImpl() = default;
    ~CoreTextDiscoveryImpl() override = default;

    std::vector<FontDescriptor> findFonts(const FontQuery& query) override;
    FontDescriptor findFallback(char32_t codepoint, FontStyle style) override;
    FontDescriptor defaultMonospace() override;
};

std::vector<FontDescriptor> CoreTextDiscoveryImpl::findFonts(const FontQuery& query) {
    std::vector<FontDescriptor> results;

    CFPtr<CFStringRef> familyName(
        CFStringCreateWithCString(kCFAllocatorDefault, query.family.c_str(),
                                  kCFStringEncodingUTF8));
    if (!familyName) return results;

    // Build attributes dictionary
    CTFontSymbolicTraits symbolicTraits = traitsFromStyle(query.style);

    CFPtr<CTFontDescriptorRef> descriptor{nullptr};

    if (symbolicTraits != 0) {
        // Create traits dictionary
        CFNumberRef symbolicNum = CFNumberCreate(
            kCFAllocatorDefault, kCFNumberSInt32Type, &symbolicTraits);
        const void* traitKeys[] = { kCTFontSymbolicTrait };
        const void* traitValues[] = { symbolicNum };
        CFPtr<CFDictionaryRef> traitDict(CFDictionaryCreate(
            kCFAllocatorDefault, traitKeys, traitValues, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
        CFRelease(symbolicNum);

        const void* attrKeys[] = { kCTFontFamilyNameAttribute, kCTFontTraitsAttribute };
        const void* attrValues[] = { familyName.get(), traitDict.get() };
        CFPtr<CFDictionaryRef> attrDict(CFDictionaryCreate(
            kCFAllocatorDefault, attrKeys, attrValues, 2,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));

        descriptor = CFPtr<CTFontDescriptorRef>(
            CTFontDescriptorCreateWithAttributes(attrDict.get()));
    } else {
        const void* attrKeys[] = { kCTFontFamilyNameAttribute };
        const void* attrValues[] = { familyName.get() };
        CFPtr<CFDictionaryRef> attrDict(CFDictionaryCreate(
            kCFAllocatorDefault, attrKeys, attrValues, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));

        descriptor = CFPtr<CTFontDescriptorRef>(
            CTFontDescriptorCreateWithAttributes(attrDict.get()));
    }

    if (!descriptor) return results;

    CFPtr<CFArrayRef> matches(
        CTFontDescriptorCreateMatchingFontDescriptors(descriptor.get(), nullptr));
    if (!matches) return results;

    CFIndex count = CFArrayGetCount(matches.get());
    for (CFIndex i = 0; i < count; ++i) {
        CTFontDescriptorRef match =
            static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(matches.get(), i));
        results.push_back(descriptorFromCT(match));
    }

    return results;
}

FontDescriptor CoreTextDiscoveryImpl::findFallback(char32_t codepoint, FontStyle style) {
    // Create a base font (Menlo, system monospace)
    CFPtr<CTFontRef> baseFont(CTFontCreateWithName(CFSTR("Menlo"), 12.0, nullptr));
    if (!baseFont) {
        baseFont = CFPtr<CTFontRef>(
            CTFontCreateUIFontForLanguage(kCTFontUIFontUserFixedPitch, 12.0, nullptr));
    }
    if (!baseFont) return {};

    // Build a string from the codepoint
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

    CFPtr<CFStringRef> str(CFStringCreateWithCharacters(kCFAllocatorDefault, characters, charCount));
    if (!str) return {};

    CFRange range = CFRangeMake(0, charCount);
    CFPtr<CTFontRef> fallbackFont(
        CTFontCreateForString(baseFont.get(), str.get(), range));
    if (!fallbackFont) return {};

    CFPtr<CTFontDescriptorRef> desc(CTFontCopyFontDescriptor(fallbackFont.get()));
    if (!desc) return {};

    return descriptorFromCT(desc.get());
}

FontDescriptor CoreTextDiscoveryImpl::defaultMonospace() {
    CFPtr<CTFontRef> font(
        CTFontCreateUIFontForLanguage(kCTFontUIFontUserFixedPitch, 0, nullptr));
    if (!font) {
        // Fallback: try Menlo directly
        font = CFPtr<CTFontRef>(CTFontCreateWithName(CFSTR("Menlo"), 12.0, nullptr));
    }
    if (!font) return {};

    CFPtr<CTFontDescriptorRef> desc(CTFontCopyFontDescriptor(font.get()));
    if (!desc) return {};

    return descriptorFromCT(desc.get());
}

std::unique_ptr<IFontDiscovery> createCoreTextDiscovery() {
    return std::make_unique<CoreTextDiscoveryImpl>();
}

} // namespace termcore
