#include "termcore/font_index.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace termcore {

namespace {

std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

} // namespace

bool FontIndex::loadFromJSON(const std::string& json) {
    try {
        auto arr = nlohmann::json::parse(json);
        if (!arr.is_array()) return false;

        fonts_.clear();
        fonts_.reserve(arr.size());

        for (const auto& obj : arr) {
            if (!obj.is_object()) continue;

            FontMetadata meta;
            meta.name = obj.value("name", "");
            if (meta.name.empty()) continue;

            meta.postscript_name = obj.value("postscript_name", "");
            meta.category = obj.value("category", "monospace");
            meta.has_ligatures = obj.value("has_ligatures", false);
            meta.has_nerd_font_variant = obj.value("has_nerd_font_variant", false);
            meta.download_url = obj.value("download_url", "");
            meta.nerd_font_download_url = obj.value("nerd_font_download_url", "");
            meta.license = obj.value("license", "");
            meta.installed = false;

            fonts_.push_back(std::move(meta));
        }

        return true;
    } catch (...) {
        return false;
    }
}

const std::vector<FontMetadata>& FontIndex::all() const {
    return fonts_;
}

size_t FontIndex::count() const {
    return fonts_.size();
}

std::vector<const FontMetadata*> FontIndex::search(const std::string& query) const {
    std::vector<const FontMetadata*> results;
    std::string lowerQuery = toLower(query);

    for (const auto& font : fonts_) {
        std::string lowerName = toLower(font.name);
        if (lowerName.find(lowerQuery) != std::string::npos) {
            results.push_back(&font);
        }
    }

    return results;
}

std::vector<const FontMetadata*> FontIndex::filter(
    bool installed_only, bool nerd_fonts_only, bool ligatures_only) const {
    std::vector<const FontMetadata*> results;

    for (const auto& font : fonts_) {
        if (installed_only && !font.installed) continue;
        if (nerd_fonts_only && !font.has_nerd_font_variant) continue;
        if (ligatures_only && !font.has_ligatures) continue;
        results.push_back(&font);
    }

    return results;
}

void FontIndex::markInstalled(const std::string& name) {
    for (auto& font : fonts_) {
        if (font.name == name) {
            font.installed = true;
            return;
        }
    }
}

void FontIndex::markUninstalled(const std::string& name) {
    for (auto& font : fonts_) {
        if (font.name == name) {
            font.installed = false;
            return;
        }
    }
}

bool FontIndex::addSystemFont(const std::string& family_name) {
    // Check for duplicates (case-insensitive)
    std::string lowerName = toLower(family_name);
    for (const auto& font : fonts_) {
        if (toLower(font.name) == lowerName) return false;
        if (!font.postscript_name.empty() && toLower(font.postscript_name) == lowerName) return false;
    }

    FontMetadata meta;
    meta.name = family_name;
    meta.category = "system";
    meta.installed = true;
    fonts_.push_back(std::move(meta));
    return true;
}

void FontIndex::setInstalledPredicate(std::function<bool(const std::string&)> pred) {
    installedPredicate_ = std::move(pred);
}

void FontIndex::refreshInstallStatus() {
    if (!installedPredicate_) return;

    for (auto& font : fonts_) {
        // Check by family name first (GDI+/CoreText use this), then postscript name
        font.installed = installedPredicate_(font.name)
                      || (!font.postscript_name.empty()
                          && installedPredicate_(font.postscript_name));
    }
}

} // namespace termcore
