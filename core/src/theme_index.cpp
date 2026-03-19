#include "termcore/theme_index.h"
#include "termcore/theme_loader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <nlohmann/json.hpp>

namespace termcore {

namespace {

namespace fs = std::filesystem;

uint32_t parseHexColor(const std::string& hex) {
    try {
        return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

bool computeIsDark(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return luminance < 128.0;
}

std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

} // namespace

bool ThemeIndex::loadFromJSON(const std::string& json) {
    try {
        auto arr = nlohmann::json::parse(json);
        if (!arr.is_array()) return false;

        themes_.clear();
        themes_.reserve(arr.size());

        for (const auto& obj : arr) {
            if (!obj.is_object()) continue;

            ThemeMetadata meta;
            meta.name = obj.value("name", "");
            if (meta.name.empty()) continue;

            meta.background = parseHexColor(obj.value("background", "000000"));
            meta.foreground = parseHexColor(obj.value("foreground", "FFFFFF"));

            if (obj.contains("palette") && obj["palette"].is_array()) {
                const auto& pal = obj["palette"];
                for (size_t i = 0; i < 16 && i < pal.size(); ++i) {
                    if (pal[i].is_string()) {
                        meta.palette[i] = parseHexColor(pal[i].get<std::string>());
                    }
                }
            }

            meta.source_url = obj.value("source_url", "");
            meta.is_dark = computeIsDark(meta.background);
            meta.installed = false;

            themes_.push_back(std::move(meta));
        }

        return true;
    } catch (...) {
        return false;
    }
}

const std::vector<ThemeMetadata>& ThemeIndex::all() const {
    return themes_;
}

std::vector<const ThemeMetadata*> ThemeIndex::search(const std::string& query) const {
    std::vector<const ThemeMetadata*> results;
    std::string lowerQuery = toLower(query);

    for (const auto& theme : themes_) {
        std::string lowerName = toLower(theme.name);
        if (lowerName.find(lowerQuery) != std::string::npos) {
            results.push_back(&theme);
        }
    }

    return results;
}

std::vector<const ThemeMetadata*> ThemeIndex::filterByCategory(
    bool dark_only, bool light_only, bool installed_only) const {
    std::vector<const ThemeMetadata*> results;

    for (const auto& theme : themes_) {
        if (dark_only && !theme.is_dark) continue;
        if (light_only && theme.is_dark) continue;
        if (installed_only && !theme.installed) continue;
        results.push_back(&theme);
    }

    return results;
}

void ThemeIndex::markInstalled(const std::string& name) {
    for (auto& theme : themes_) {
        if (theme.name == name) {
            theme.installed = true;
            return;
        }
    }
}

void ThemeIndex::refreshInstallStatus() {
    std::string themeDir = defaultThemeDir();
    std::error_code ec;

    if (!fs::is_directory(themeDir, ec)) {
        for (auto& theme : themes_) {
            theme.installed = false;
        }
        return;
    }

    // Collect installed theme file names (stems)
    std::vector<std::string> installedNames;
    for (const auto& entry : fs::directory_iterator(themeDir, ec)) {
        if (entry.is_regular_file(ec)) {
            installedNames.push_back(entry.path().stem().string());
        }
    }

    for (auto& theme : themes_) {
        theme.installed = std::find(installedNames.begin(), installedNames.end(),
                                     theme.name) != installedNames.end();
    }
}

size_t ThemeIndex::count() const {
    return themes_.size();
}

} // namespace termcore
