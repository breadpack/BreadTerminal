#include "FontconfigDiscovery.h"

#include <fontconfig/fontconfig.h>
#include <string>
#include <vector>

namespace termcore {

namespace {

/// RAII wrapper for FcPattern.
class FcPatternPtr {
public:
    explicit FcPatternPtr(FcPattern* p = nullptr) : pat_(p) {}
    ~FcPatternPtr() {
        if (pat_) FcPatternDestroy(pat_);
    }
    FcPatternPtr(const FcPatternPtr&) = delete;
    FcPatternPtr& operator=(const FcPatternPtr&) = delete;
    FcPatternPtr(FcPatternPtr&& o) noexcept : pat_(o.pat_) {
        o.pat_ = nullptr;
    }
    FcPattern* get() const { return pat_; }
    explicit operator bool() const { return pat_ != nullptr; }

private:
    FcPattern* pat_;
};

/// RAII wrapper for FcFontSet.
class FcFontSetPtr {
public:
    explicit FcFontSetPtr(FcFontSet* s = nullptr) : set_(s) {}
    ~FcFontSetPtr() {
        if (set_) FcFontSetDestroy(set_);
    }
    FcFontSetPtr(const FcFontSetPtr&) = delete;
    FcFontSetPtr& operator=(const FcFontSetPtr&) = delete;
    FcFontSet* get() const { return set_; }

private:
    FcFontSet* set_;
};

/// Extract a string property from an FcPattern.
std::string fcGetString(FcPattern* pat, const char* field) {
    FcChar8* value = nullptr;
    if (FcPatternGetString(pat, field, 0, &value) == FcResultMatch &&
        value) {
        return std::string(reinterpret_cast<const char*>(value));
    }
    return {};
}

/// Extract an integer property from an FcPattern.
int fcGetInt(FcPattern* pat, const char* field, int fallback) {
    int value = fallback;
    FcPatternGetInteger(pat, field, 0, &value);
    return value;
}

/// Convert fontconfig weight to CSS weight (100-900).
int fcWeightToCss(int fc_weight) {
    // FC_WEIGHT_THIN=0, LIGHT=50, REGULAR=80, MEDIUM=100,
    // BOLD=200, BLACK=210
    if (fc_weight <= 0) return 100;
    if (fc_weight <= 50) return 300;
    if (fc_weight <= 80) return 400;
    if (fc_weight <= 100) return 500;
    if (fc_weight <= 200) return 700;
    return 900;
}

/// Convert fontconfig slant + weight to FontStyle.
FontStyle fcToFontStyle(FcPattern* pat) {
    int slant = fcGetInt(pat, FC_SLANT, FC_SLANT_ROMAN);
    int weight = fcGetInt(pat, FC_WEIGHT, FC_WEIGHT_REGULAR);

    bool bold = (weight >= FC_WEIGHT_BOLD);
    bool italic = (slant != FC_SLANT_ROMAN);

    if (bold && italic) return FontStyle::BoldItalic;
    if (bold) return FontStyle::Bold;
    if (italic) return FontStyle::Italic;
    return FontStyle::Regular;
}

/// Build a FontDescriptor from an FcPattern.
FontDescriptor descriptorFromFc(FcPattern* pat) {
    FontDescriptor fd;
    fd.family = fcGetString(pat, FC_FAMILY);
    fd.postscript_name = fcGetString(pat, FC_POSTSCRIPT_NAME);
    fd.file_path = fcGetString(pat, FC_FILE);
    fd.face_index = fcGetInt(pat, FC_INDEX, 0);
    fd.style = fcToFontStyle(pat);
    fd.weight = fcWeightToCss(fcGetInt(pat, FC_WEIGHT, FC_WEIGHT_REGULAR));
    return fd;
}

/// Map FontStyle to fontconfig slant + weight values.
void applyStyleToPattern(FcPattern* pat, FontStyle style) {
    auto s = static_cast<uint8_t>(style);
    if (s & static_cast<uint8_t>(FontStyle::Bold)) {
        FcPatternAddInteger(pat, FC_WEIGHT, FC_WEIGHT_BOLD);
    }
    if (s & static_cast<uint8_t>(FontStyle::Italic)) {
        FcPatternAddInteger(pat, FC_SLANT, FC_SLANT_ITALIC);
    }
}

} // namespace

class FontconfigDiscoveryImpl : public IFontDiscovery {
public:
    FontconfigDiscoveryImpl() { FcInit(); }

    ~FontconfigDiscoveryImpl() override = default;

    std::vector<FontDescriptor> findFonts(const FontQuery& query) override {
        std::vector<FontDescriptor> results;

        FcPatternPtr pat(FcPatternCreate());
        if (!pat) return results;

        FcPatternAddString(
            pat.get(), FC_FAMILY,
            reinterpret_cast<const FcChar8*>(query.family.c_str()));

        applyStyleToPattern(pat.get(), query.style);

        FcConfigSubstitute(nullptr, pat.get(), FcMatchPattern);
        FcDefaultSubstitute(pat.get());

        FcResult fc_result;
        FcFontSetPtr font_set(
            FcFontSort(nullptr, pat.get(), FcFalse, nullptr, &fc_result));
        if (!font_set.get()) return results;

        int count = font_set.get()->nfont;
        for (int i = 0; i < count && i < 20; ++i) {
            FcPattern* match = font_set.get()->fonts[i];
            results.push_back(descriptorFromFc(match));
        }

        return results;
    }

    FontDescriptor findFallback(char32_t codepoint,
                                FontStyle style) override {
        FcPatternPtr pat(FcPatternCreate());
        if (!pat) return {};

        // Add monospace preference
        FcPatternAddString(pat.get(), FC_FAMILY,
                           reinterpret_cast<const FcChar8*>("monospace"));

        applyStyleToPattern(pat.get(), style);

        // Create a charset containing the codepoint
        FcCharSet* charset = FcCharSetCreate();
        if (!charset) return {};
        FcCharSetAddChar(charset, static_cast<FcChar32>(codepoint));
        FcPatternAddCharSet(pat.get(), FC_CHARSET, charset);
        FcCharSetDestroy(charset);

        FcConfigSubstitute(nullptr, pat.get(), FcMatchPattern);
        FcDefaultSubstitute(pat.get());

        FcResult fc_result;
        FcPattern* match =
            FcFontMatch(nullptr, pat.get(), &fc_result);
        if (!match) return {};

        FontDescriptor fd = descriptorFromFc(match);
        FcPatternDestroy(match);
        return fd;
    }

    FontDescriptor defaultMonospace() override {
        FcPatternPtr pat(FcPatternCreate());
        if (!pat) return {};

        FcPatternAddString(pat.get(), FC_FAMILY,
                           reinterpret_cast<const FcChar8*>("monospace"));

        FcConfigSubstitute(nullptr, pat.get(), FcMatchPattern);
        FcDefaultSubstitute(pat.get());

        FcResult fc_result;
        FcPattern* match =
            FcFontMatch(nullptr, pat.get(), &fc_result);
        if (!match) return {};

        FontDescriptor fd = descriptorFromFc(match);
        FcPatternDestroy(match);
        return fd;
    }
};

std::unique_ptr<IFontDiscovery> createFontconfigDiscovery() {
    return std::make_unique<FontconfigDiscoveryImpl>();
}

} // namespace termcore
