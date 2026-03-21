#if defined(_WIN32)

#include "DirectWriteDiscovery.h"

#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace termcore {

namespace {

/// Convert a wide string to UTF-8.
std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                  static_cast<int>(wide.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()),
                        result.data(), len, nullptr, nullptr);
    return result;
}

/// Convert UTF-8 to wide string.
std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                   static_cast<int>(utf8.size()),
                                   nullptr, 0);
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()),
                        result.data(), wlen);
    return result;
}

/// Get a localized string from an IDWriteLocalizedStrings (prefer "en-us").
std::string getLocalizedName(IDWriteLocalizedStrings* names) {
    if (!names || names->GetCount() == 0) return {};

    UINT32 index = 0;
    BOOL exists = FALSE;
    names->FindLocaleName(L"en-us", &index, &exists);
    if (!exists) index = 0;

    UINT32 length = 0;
    names->GetStringLength(index, &length);
    if (length == 0) return {};

    std::wstring wname(length + 1, L'\0');
    names->GetString(index, wname.data(), length + 1);
    wname.resize(length);

    return wideToUtf8(wname);
}

/// Convert DWRITE_FONT_WEIGHT to CSS weight.
int dwWeightToCss(DWRITE_FONT_WEIGHT weight) {
    // DWRITE_FONT_WEIGHT values map roughly 1:1 to CSS weights
    return static_cast<int>(weight);
}

/// Derive FontStyle from DirectWrite weight + style.
FontStyle dwToFontStyle(DWRITE_FONT_WEIGHT weight,
                        DWRITE_FONT_STYLE style) {
    bool bold = (weight >= DWRITE_FONT_WEIGHT_BOLD);
    bool italic = (style == DWRITE_FONT_STYLE_ITALIC ||
                   style == DWRITE_FONT_STYLE_OBLIQUE);

    if (bold && italic) return FontStyle::BoldItalic;
    if (bold) return FontStyle::Bold;
    if (italic) return FontStyle::Italic;
    return FontStyle::Regular;
}

/// Get the file path for a font face.
std::string getFontFilePath(IDWriteFontFace* fontFace) {
    UINT32 fileCount = 0;
    fontFace->GetFiles(&fileCount, nullptr);
    if (fileCount == 0) return {};

    std::vector<ComPtr<IDWriteFontFile>> files(fileCount);
    std::vector<IDWriteFontFile*> rawPtrs(fileCount);
    fontFace->GetFiles(&fileCount, rawPtrs.data());
    for (UINT32 i = 0; i < fileCount; ++i) {
        files[i].Attach(rawPtrs[i]);
    }

    if (!files[0]) return {};

    const void* refKey = nullptr;
    UINT32 refKeySize = 0;
    files[0]->GetReferenceKey(&refKey, &refKeySize);

    ComPtr<IDWriteFontFileLoader> loader;
    files[0]->GetLoader(loader.GetAddressOf());
    if (!loader) return {};

    ComPtr<IDWriteLocalFontFileLoader> localLoader;
    HRESULT hr = loader.As(&localLoader);
    if (FAILED(hr)) return {};

    UINT32 pathLen = 0;
    localLoader->GetFilePathLengthFromKey(refKey, refKeySize, &pathLen);
    if (pathLen == 0) return {};

    std::wstring wpath(pathLen + 1, L'\0');
    localLoader->GetFilePathFromKey(refKey, refKeySize,
                                    wpath.data(), pathLen + 1);
    wpath.resize(pathLen);

    return wideToUtf8(wpath);
}

/// Build a FontDescriptor from a DirectWrite font.
FontDescriptor descriptorFromDWrite(IDWriteFont* font,
                                    IDWriteFontFamily* family) {
    FontDescriptor fd;

    // Family name
    ComPtr<IDWriteLocalizedStrings> familyNames;
    family->GetFamilyNames(familyNames.GetAddressOf());
    fd.family = getLocalizedName(familyNames.Get());

    // Face name (postscript-like)
    ComPtr<IDWriteLocalizedStrings> faceNames;
    font->GetFaceNames(faceNames.GetAddressOf());
    std::string faceName = getLocalizedName(faceNames.Get());
    fd.postscript_name = fd.family + "-" + faceName;

    // Style
    fd.style = dwToFontStyle(font->GetWeight(), font->GetStyle());
    fd.weight = dwWeightToCss(font->GetWeight());

    // File path via font face
    ComPtr<IDWriteFontFace> fontFace;
    HRESULT hr = font->CreateFontFace(fontFace.GetAddressOf());
    if (SUCCEEDED(hr)) {
        fd.file_path = getFontFilePath(fontFace.Get());
        fd.face_index = static_cast<int>(fontFace->GetIndex());
    }

    return fd;
}

} // namespace

class DirectWriteDiscoveryImpl : public IFontDiscovery {
public:
    DirectWriteDiscoveryImpl() {
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory_.GetAddressOf()));
    }

    ~DirectWriteDiscoveryImpl() override = default;

    std::vector<FontDescriptor> findFonts(const FontQuery& query) override {
        std::vector<FontDescriptor> results;
        if (!factory_) return results;

        ComPtr<IDWriteFontCollection> collection;
        factory_->GetSystemFontCollection(collection.GetAddressOf(), TRUE);
        if (!collection) return results;

        std::wstring wfamily = utf8ToWide(query.family);
        UINT32 familyIndex = 0;
        BOOL exists = FALSE;
        collection->FindFamilyName(wfamily.c_str(), &familyIndex, &exists);
        if (!exists) return results;

        ComPtr<IDWriteFontFamily> family;
        collection->GetFontFamily(familyIndex, family.GetAddressOf());
        if (!family) return results;

        UINT32 count = family->GetFontCount();
        for (UINT32 i = 0; i < count && results.size() < 20; ++i) {
            ComPtr<IDWriteFont> font;
            family->GetFont(i, font.GetAddressOf());
            if (!font) continue;

            results.push_back(descriptorFromDWrite(font.Get(), family.Get()));
        }

        return results;
    }

    FontDescriptor findFallback(char32_t codepoint,
                                FontStyle style) override {
        if (!factory_) return {};

        ComPtr<IDWriteFontCollection> collection;
        factory_->GetSystemFontCollection(collection.GetAddressOf());
        if (!collection) return {};

        // Iterate families to find one containing the codepoint
        UINT32 familyCount = collection->GetFontFamilyCount();
        for (UINT32 fi = 0; fi < familyCount; ++fi) {
            ComPtr<IDWriteFontFamily> family;
            collection->GetFontFamily(fi, family.GetAddressOf());
            if (!family) continue;

            ComPtr<IDWriteFont> font;
            family->GetFirstMatchingFont(
                DWRITE_FONT_WEIGHT_REGULAR,
                DWRITE_FONT_STRETCH_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                font.GetAddressOf());
            if (!font) continue;

            BOOL hasGlyph = FALSE;
            font->HasCharacter(static_cast<UINT32>(codepoint), &hasGlyph);
            if (!hasGlyph) continue;

            return descriptorFromDWrite(font.Get(), family.Get());
        }

        return {};
    }

    FontDescriptor defaultMonospace() override {
        if (!factory_) return {};

        ComPtr<IDWriteFontCollection> collection;
        factory_->GetSystemFontCollection(collection.GetAddressOf());
        if (!collection) return {};

        // Try common monospace fonts in preference order
        const wchar_t* candidates[] = {
            L"Cascadia Code", L"Cascadia Mono", L"Consolas",
            L"Courier New", L"Lucida Console"
        };

        for (const auto* name : candidates) {
            UINT32 familyIndex = 0;
            BOOL exists = FALSE;
            collection->FindFamilyName(name, &familyIndex, &exists);
            if (!exists) continue;

            ComPtr<IDWriteFontFamily> family;
            collection->GetFontFamily(familyIndex, family.GetAddressOf());
            if (!family) continue;

            ComPtr<IDWriteFont> font;
            family->GetFirstMatchingFont(
                DWRITE_FONT_WEIGHT_REGULAR,
                DWRITE_FONT_STRETCH_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                font.GetAddressOf());
            if (!font) continue;

            return descriptorFromDWrite(font.Get(), family.Get());
        }

        return {};
    }

private:
    ComPtr<IDWriteFactory> factory_;
};

std::unique_ptr<IFontDiscovery> createDirectWriteDiscovery() {
    return std::make_unique<DirectWriteDiscoveryImpl>();
}

} // namespace termcore

#endif // _WIN32
