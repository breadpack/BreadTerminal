#include "termcore/theme_importer.h"
#include "termcore/theme_loader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace termcore {

namespace {

namespace fs = std::filesystem;

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string fileExtension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot);
    return toLower(ext);
}

std::optional<uint32_t> parseColor(const std::string& s) {
    std::string hex = trim(s);
    if (hex.empty()) return std::nullopt;
    if (hex[0] == '#') hex = hex.substr(1);
    if (hex.size() != 6) return std::nullopt;
    try {
        return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    } catch (...) {
        return std::nullopt;
    }
}

/// Parse an iTerm2 color component from XML plist.
/// iTerm2 uses floating-point color components (0.0-1.0) in dictionaries like:
///   <key>Background Color</key>
///   <dict>
///     <key>Red Component</key>
///     <real>0.156863</real>
///     ...
///   </dict>
struct ITerm2Color {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
};

/// Simple XML tag value extractor — finds <real>value</real> after a <key>name</key>.
std::string findXmlValue(const std::string& xml, size_t start, size_t end,
                          const std::string& key_name) {
    // Find <key>key_name</key> within the range
    std::string key_tag = "<key>" + key_name + "</key>";
    auto pos = xml.find(key_tag, start);
    if (pos == std::string::npos || pos >= end) return "";

    pos += key_tag.size();

    // Skip whitespace
    while (pos < end && std::isspace(static_cast<unsigned char>(xml[pos])))
        ++pos;

    // Look for <real>...</real>
    std::string real_open = "<real>";
    std::string real_close = "</real>";
    auto real_start = xml.find(real_open, pos);
    if (real_start == std::string::npos || real_start >= end) return "";
    real_start += real_open.size();
    auto real_end = xml.find(real_close, real_start);
    if (real_end == std::string::npos || real_end >= end) return "";

    return trim(xml.substr(real_start, real_end - real_start));
}

/// Extract a color dict from iTerm2 plist XML.
std::optional<uint32_t> parseITerm2ColorDict(const std::string& xml,
                                              const std::string& color_key) {
    std::string key_tag = "<key>" + color_key + "</key>";
    auto key_pos = xml.find(key_tag);
    if (key_pos == std::string::npos) return std::nullopt;

    // Find the <dict> after this key
    auto dict_start = xml.find("<dict>", key_pos + key_tag.size());
    if (dict_start == std::string::npos) return std::nullopt;
    auto dict_end = xml.find("</dict>", dict_start);
    if (dict_end == std::string::npos) return std::nullopt;
    dict_end += 7; // include </dict>

    auto red_str = findXmlValue(xml, dict_start, dict_end, "Red Component");
    auto green_str = findXmlValue(xml, dict_start, dict_end, "Green Component");
    auto blue_str = findXmlValue(xml, dict_start, dict_end, "Blue Component");

    if (red_str.empty() && green_str.empty() && blue_str.empty())
        return std::nullopt;

    try {
        double r = red_str.empty() ? 0.0 : std::stod(red_str);
        double g = green_str.empty() ? 0.0 : std::stod(green_str);
        double b = blue_str.empty() ? 0.0 : std::stod(blue_str);

        auto clamp = [](double v) -> uint8_t {
            if (v < 0.0) return 0;
            if (v > 1.0) return 255;
            return static_cast<uint8_t>(v * 255.0 + 0.5);
        };

        uint32_t color = (static_cast<uint32_t>(clamp(r)) << 16) |
                          (static_cast<uint32_t>(clamp(g)) << 8) |
                          static_cast<uint32_t>(clamp(b));
        return color;
    } catch (...) {
        return std::nullopt;
    }
}

/// Simple TOML/YAML value extractor for Alacritty themes.
/// Alacritty uses TOML (newer) or YAML (older) with sections like:
///   [colors.primary]
///   background = "#1e1e2e"
///   foreground = "#cdd6f4"
///   [colors.normal]
///   black = "#45475a"
///   ...
struct AlacrittySection {
    std::string name;
    std::unordered_map<std::string, std::string> values;
};

std::vector<AlacrittySection> parseAlacrittyToml(const std::string& content) {
    std::vector<AlacrittySection> sections;
    AlacrittySection current;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        // Section header: [section.name]
        if (trimmed[0] == '[') {
            if (!current.name.empty() || !current.values.empty()) {
                sections.push_back(std::move(current));
                current = {};
            }
            auto close = trimmed.find(']');
            if (close != std::string::npos) {
                current.name = trim(trimmed.substr(1, close - 1));
            }
            continue;
        }

        // Key-value pair
        auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            // Try YAML-style colon
            auto colon = trimmed.find(':');
            if (colon != std::string::npos) {
                std::string key = trim(trimmed.substr(0, colon));
                std::string val = trim(trimmed.substr(colon + 1));
                // Remove quotes
                if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'')) {
                    val = val.substr(1, val.size() - 2);
                }
                current.values[key] = val;
            }
            continue;
        }

        std::string key = trim(trimmed.substr(0, eq));
        std::string val = trim(trimmed.substr(eq + 1));
        // Remove quotes
        if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'')) {
            val = val.substr(1, val.size() - 2);
        }
        current.values[key] = val;
    }

    if (!current.name.empty() || !current.values.empty()) {
        sections.push_back(std::move(current));
    }

    return sections;
}

} // namespace

ThemeImportFormat detectImportFormat(const std::string& content,
                                     const std::string& path) {
    std::string ext = fileExtension(path);

    // Extension-based detection
    if (ext == ".json") return ThemeImportFormat::WindowsTerminal;
    if (ext == ".itermcolors" || ext == ".plist" || ext == ".xml")
        return ThemeImportFormat::ITerm2;
    if (ext == ".toml" || ext == ".yml" || ext == ".yaml")
        return ThemeImportFormat::Alacritty;
    if (ext == ".conf") return ThemeImportFormat::Kitty;

    // Content-based detection
    std::string trimmed = trim(content);

    // XML plist (iTerm2)
    if (trimmed.find("<?xml") != std::string::npos ||
        trimmed.find("<plist") != std::string::npos ||
        trimmed.find("Red Component") != std::string::npos) {
        return ThemeImportFormat::ITerm2;
    }

    // JSON
    if (!trimmed.empty() && trimmed[0] == '{') {
        return ThemeImportFormat::WindowsTerminal;
    }

    // Alacritty TOML sections
    if (trimmed.find("[colors") != std::string::npos) {
        return ThemeImportFormat::Alacritty;
    }

    // Alacritty YAML style (colors: section with indented primary:)
    if (trimmed.find("colors:") != std::string::npos) {
        return ThemeImportFormat::Alacritty;
    }

    // Ghostty key=value with palette entries
    if (content.find("palette =") != std::string::npos ||
        content.find("palette=") != std::string::npos ||
        content.find("background =") != std::string::npos ||
        content.find("background=") != std::string::npos) {
        return ThemeImportFormat::Ghostty;
    }

    // Kitty space-separated
    if (content.find("color0 ") != std::string::npos ||
        content.find("background ") != std::string::npos) {
        return ThemeImportFormat::Kitty;
    }

    return ThemeImportFormat::Ghostty; // fallback
}

std::optional<Theme> importFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    std::string name = fs::path(path).stem().string();
    auto format = detectImportFormat(content, path);

    switch (format) {
    case ThemeImportFormat::Ghostty:
        return importFromGhostty(content, name);
    case ThemeImportFormat::Kitty:
        return importFromKitty(content, name);
    case ThemeImportFormat::WindowsTerminal:
        return importFromWindowsTerminal(content, name);
    case ThemeImportFormat::ITerm2:
        return importFromITerm2(content, name);
    case ThemeImportFormat::Alacritty:
        return importFromAlacritty(content, name);
    case ThemeImportFormat::Auto:
        break;
    }

    // Auto fallback: try all formats
    if (auto t = importFromWindowsTerminal(content, name)) return t;
    if (auto t = importFromITerm2(content, name)) return t;
    if (auto t = importFromAlacritty(content, name)) return t;
    if (auto t = importFromGhostty(content, name)) return t;
    if (auto t = importFromKitty(content, name)) return t;
    return std::nullopt;
}

std::optional<Theme> importFromGhostty(const std::string& content,
                                        const std::string& name) {
    return parseThemeString(content, name, ThemeFormat::Ghostty);
}

std::optional<Theme> importFromKitty(const std::string& content,
                                      const std::string& name) {
    return parseThemeString(content, name, ThemeFormat::Kitty);
}

std::optional<Theme> importFromWindowsTerminal(const std::string& json,
                                                const std::string& name) {
    return parseThemeString(json, name, ThemeFormat::WindowsTerminal);
}

std::optional<Theme> importFromITerm2(const std::string& xml,
                                       const std::string& name) {
    Theme theme{};
    theme.name = name;
    bool hasAnyColor = false;

    // Try to extract theme name from plist
    // Look for a key like "name" in the plist
    auto namePos = xml.find("<key>name</key>");
    if (namePos == std::string::npos) {
        namePos = xml.find("<key>Name</key>");
    }
    if (namePos != std::string::npos) {
        auto strOpen = xml.find("<string>", namePos);
        auto strClose = xml.find("</string>", namePos);
        if (strOpen != std::string::npos && strClose != std::string::npos) {
            strOpen += 8;
            if (strOpen < strClose) {
                theme.name = xml.substr(strOpen, strClose - strOpen);
            }
        }
    }

    // Background Color
    auto bg = parseITerm2ColorDict(xml, "Background Color");
    if (bg) {
        theme.background = *bg;
        hasAnyColor = true;
    }

    // Foreground Color
    auto fg = parseITerm2ColorDict(xml, "Foreground Color");
    if (fg) {
        theme.foreground = *fg;
        hasAnyColor = true;
    }

    // Cursor Color
    auto cursor = parseITerm2ColorDict(xml, "Cursor Color");
    if (cursor) theme.cursor_color = *cursor;

    // Selection Color
    auto selBg = parseITerm2ColorDict(xml, "Selection Color");
    if (selBg) theme.selection_background = *selBg;

    auto selFg = parseITerm2ColorDict(xml, "Selected Text Color");
    if (selFg) theme.selection_foreground = *selFg;

    // ANSI colors (0-15)
    // iTerm2 uses "Ansi 0 Color" through "Ansi 15 Color"
    for (int i = 0; i < 16; ++i) {
        std::string key = "Ansi " + std::to_string(i) + " Color";
        auto c = parseITerm2ColorDict(xml, key);
        if (c) {
            theme.palette[i] = *c;
            hasAnyColor = true;
        }
    }

    if (!hasAnyColor) return std::nullopt;
    return theme;
}

std::optional<Theme> importFromAlacritty(const std::string& content,
                                          const std::string& name) {
    Theme theme{};
    theme.name = name;
    bool hasAnyColor = false;

    auto sections = parseAlacrittyToml(content);

    // Also handle flat YAML format with indentation
    // colors:
    //   primary:
    //     background: '#1e1e2e'
    // We handle this by looking at section names containing "colors"

    for (const auto& section : sections) {
        std::string sectionLower = toLower(section.name);

        if (sectionLower == "colors.primary" || sectionLower == "primary") {
            for (const auto& [key, val] : section.values) {
                if (key == "background") {
                    auto c = parseColor(val);
                    if (c) { theme.background = *c; hasAnyColor = true; }
                } else if (key == "foreground") {
                    auto c = parseColor(val);
                    if (c) { theme.foreground = *c; hasAnyColor = true; }
                }
            }
        } else if (sectionLower == "colors.cursor" || sectionLower == "cursor") {
            for (const auto& [key, val] : section.values) {
                if (key == "cursor") {
                    auto c = parseColor(val);
                    if (c) theme.cursor_color = *c;
                } else if (key == "text") {
                    // cursor text color, not directly mapped
                }
            }
        } else if (sectionLower == "colors.selection" || sectionLower == "selection") {
            for (const auto& [key, val] : section.values) {
                if (key == "background") {
                    auto c = parseColor(val);
                    if (c) theme.selection_background = *c;
                } else if (key == "text") {
                    auto c = parseColor(val);
                    if (c) theme.selection_foreground = *c;
                }
            }
        } else if (sectionLower == "colors.normal" || sectionLower == "normal") {
            // Normal colors: indices 0-7
            static const std::pair<const char*, int> normalMap[] = {
                {"black", 0}, {"red", 1},   {"green", 2}, {"yellow", 3},
                {"blue", 4},  {"magenta", 5}, {"cyan", 6},  {"white", 7},
            };
            for (const auto& [colorName, idx] : normalMap) {
                auto it = section.values.find(colorName);
                if (it != section.values.end()) {
                    auto c = parseColor(it->second);
                    if (c) { theme.palette[idx] = *c; hasAnyColor = true; }
                }
            }
        } else if (sectionLower == "colors.bright" || sectionLower == "bright") {
            // Bright colors: indices 8-15
            static const std::pair<const char*, int> brightMap[] = {
                {"black", 8},  {"red", 9},    {"green", 10}, {"yellow", 11},
                {"blue", 12},  {"magenta", 13}, {"cyan", 14},  {"white", 15},
            };
            for (const auto& [colorName, idx] : brightMap) {
                auto it = section.values.find(colorName);
                if (it != section.values.end()) {
                    auto c = parseColor(it->second);
                    if (c) { theme.palette[idx] = *c; hasAnyColor = true; }
                }
            }
        }
    }

    if (!hasAnyColor) return std::nullopt;
    return theme;
}

} // namespace termcore
