#include "termcore/ghostty_config_import.h"
#include "termcore/theme_loader.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>

namespace termcore {

namespace {

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool parseBool(const std::string& value) {
    return value == "true" || value == "1" || value == "yes";
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG);
}

/// Map Ghostty keybind actions to BreadTerminal actions.
std::string mapGhosttyAction(const std::string& action) {
    static const std::unordered_map<std::string, std::string> action_map = {
        {"copy_to_clipboard", "copy"},
        {"paste_from_clipboard", "paste"},
        {"new_tab", "new_tab"},
        {"close_surface", "close_tab"},
        {"new_split:right", "split_right"},
        {"new_split:down", "split_down"},
        {"goto_split:previous", "focus_previous_pane"},
        {"goto_split:next", "focus_next_pane"},
        {"goto_split:left", "focus_left"},
        {"goto_split:right", "focus_right"},
        {"goto_split:up", "focus_up"},
        {"goto_split:down", "focus_down"},
        {"increase_font_size:1", "increase_font_size"},
        {"decrease_font_size:1", "decrease_font_size"},
        {"reset_font_size", "reset_font_size"},
        {"toggle_fullscreen", "toggle_fullscreen"},
        {"scroll_page_up", "scroll_page_up"},
        {"scroll_page_down", "scroll_page_down"},
        {"scroll_to_top", "scroll_to_top"},
        {"scroll_to_bottom", "scroll_to_bottom"},
    };

    auto it = action_map.find(action);
    if (it != action_map.end()) return it->second;
    return action;  // pass through unknown actions
}

/// Map Ghostty key modifier names to BreadTerminal format.
/// Ghostty uses: ctrl, shift, alt, super
/// BreadTerminal uses: ctrl, shift, alt, cmd (macOS) / super
std::string mapGhosttyTrigger(const std::string& trigger) {
    std::string result = trigger;
    // Ghostty uses "super" which maps to "cmd" on macOS
#ifdef __APPLE__
    // Replace "super+" with "cmd+"
    std::string::size_type pos = 0;
    while ((pos = result.find("super+", pos)) != std::string::npos) {
        result.replace(pos, 6, "cmd+");
        pos += 4;
    }
    // Handle standalone "super" at start
    if (result.substr(0, 5) == "super") {
        result.replace(0, 5, "cmd");
    }
#endif
    return result;
}

/// Set of Ghostty config keys that have no BreadTerminal equivalent.
const std::unordered_set<std::string>& unsupportedGhosttyKeys() {
    static const std::unordered_set<std::string> keys = {
        "adjust-cell-width",
        "adjust-cell-height",
        "bold-is-bright",
        "clipboard-read",
        "clipboard-write",
        "clipboard-trim-trailing-spaces",
        "command",
        "custom-shader",
        "custom-shader-animation",
        "desktop-notifications",
        "focus-follows-mouse",
        "font-style",
        "font-style-bold",
        "font-style-italic",
        "font-style-bold-italic",
        "freetype-load-flags",
        "gtk-single-instance",
        "gtk-tabs-location",
        "gtk-wide-tabs",
        "initial-command",
        "link-url",
        "linux-cgroup",
        "macos-non-native-fullscreen",
        "macos-option-as-alt",
        "macos-titlebar-style",
        "macos-window-shadow",
        "minimum-contrast",
        "mouse-scroll-multiplier",
        "quit-after-last-window-closed",
        "resize-overlay",
        "selection-invert-fg-bg",
        "shell-integration",
        "shell-integration-features",
        "unfocused-split-opacity",
        "wait-after-command",
        "window-decoration",
        "window-inherit-font-size",
        "window-inherit-working-directory",
        "window-save-state",
        "window-step-resize",
        "window-theme",
        "window-title-font-family",
        "working-directory",
    };
    return keys;
}

} // anonymous namespace

uint32_t GhosttyConfigImporter::parseColor(const std::string& hex) {
    std::string h = trim(hex);
    if (!h.empty() && h[0] == '#') {
        h = h.substr(1);
    }
    if (h.size() != 6) return 0;
    return static_cast<uint32_t>(std::stoul(h, nullptr, 16));
}

void GhosttyConfigImporter::mapKeybind(const std::string& value,
                                        Config& config) {
    auto eq = value.find('=');
    if (eq == std::string::npos) return;

    std::string trigger = trim(value.substr(0, eq));
    std::string action = trim(value.substr(eq + 1));

    KeyBinding kb;
    kb.trigger = mapGhosttyTrigger(trigger);
    kb.action = mapGhosttyAction(action);
    config.keybindings.push_back(std::move(kb));
}

void GhosttyConfigImporter::parseLine(const std::string& line, Config& config,
                                       std::vector<std::string>& warnings,
                                       int& imported_count,
                                       int& skipped_count) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') return;

    auto eq = trimmed.find('=');
    if (eq == std::string::npos) return;

    std::string key = trim(trimmed.substr(0, eq));
    std::string value = trim(trimmed.substr(eq + 1));

    if (key.empty()) return;

    // Check explicitly unsupported keys first
    if (unsupportedGhosttyKeys().count(key)) {
        warnings.push_back("Unsupported Ghostty option: " + key);
        ++skipped_count;
        return;
    }

    // Map Ghostty keys to BreadTerminal Config fields
    if (key == "font-family") {
        config.font_family = value;
        ++imported_count;
    } else if (key == "font-size") {
        config.font_size = std::stof(value);
        ++imported_count;
    } else if (key == "font-feature") {
        config.font_features.push_back(value);
        ++imported_count;
    } else if (key == "theme") {
        // Try to load from BreadTerminal's theme system first,
        // then fall back to Ghostty theme files.
        config.theme = value;
        auto theme = findTheme(value);
        if (theme) {
            applyTheme(config, *theme);
            ++imported_count;
        } else if (!importTheme(value, config, warnings)) {
            warnings.push_back(
                "Theme not found: " + value +
                " (not in BreadTerminal or Ghostty theme dirs)");
            ++skipped_count;
        } else {
            ++imported_count;
        }
    } else if (key == "background-opacity") {
        config.background_opacity = std::clamp(std::stof(value), 0.0f, 1.0f);
        ++imported_count;
    } else if (key == "background") {
        config.background = parseColor(value);
        ++imported_count;
    } else if (key == "foreground") {
        config.foreground = parseColor(value);
        ++imported_count;
    } else if (key == "cursor-style") {
        // Ghostty: block, underline, bar (same as BreadTerminal)
        config.cursor_style = value;
        ++imported_count;
    } else if (key == "cursor-style-blink") {
        config.cursor_blink = parseBool(value);
        ++imported_count;
    } else if (key == "cursor-color") {
        config.cursor_color = parseColor(value);
        ++imported_count;
    } else if (key == "selection-background") {
        config.selection_background = parseColor(value);
        ++imported_count;
    } else if (key == "selection-foreground") {
        config.selection_foreground = parseColor(value);
        ++imported_count;
    } else if (key == "window-padding-x") {
        // No direct BreadTerminal equivalent
        config.raw["window-padding-x"] = value;
        warnings.push_back(
            "Ghostty 'window-padding-x' stored as raw config "
            "(no direct BreadTerminal field)");
        ++skipped_count;
    } else if (key == "window-padding-y") {
        config.raw["window-padding-y"] = value;
        warnings.push_back(
            "Ghostty 'window-padding-y' stored as raw config "
            "(no direct BreadTerminal field)");
        ++skipped_count;
    } else if (key == "scrollback-limit") {
        config.scrollback_limit = std::stoi(value);
        ++imported_count;
    } else if (key == "mouse-hide-while-typing") {
        config.raw["mouse-hide-while-typing"] = value;
        warnings.push_back(
            "Ghostty 'mouse-hide-while-typing' stored as raw config "
            "(no direct BreadTerminal field)");
        ++skipped_count;
    } else if (key == "copy-on-select") {
        config.raw["copy-on-select"] = value;
        warnings.push_back(
            "Ghostty 'copy-on-select' stored as raw config "
            "(no direct BreadTerminal field)");
        ++skipped_count;
    } else if (key == "confirm-close-surface") {
        config.raw["confirm-close-surface"] = value;
        warnings.push_back(
            "Ghostty 'confirm-close-surface' stored as raw config "
            "(no direct BreadTerminal field)");
        ++skipped_count;
    } else if (key == "palette") {
        // Ghostty format: "N=RRGGBB" or "N=#RRGGBB"
        auto palette_eq = value.find('=');
        if (palette_eq != std::string::npos) {
            int idx = std::stoi(value.substr(0, palette_eq));
            if (idx >= 0 && idx < 16) {
                config.palette[idx] =
                    parseColor(trim(value.substr(palette_eq + 1)));
                ++imported_count;
            } else {
                warnings.push_back(
                    "Palette index out of range: " + std::to_string(idx));
                ++skipped_count;
            }
        } else {
            warnings.push_back("Invalid palette format: " + value);
            ++skipped_count;
        }
    } else if (key == "keybind") {
        mapKeybind(value, config);
        ++imported_count;
    } else if (key == "window-width") {
        config.window_width = std::stoi(value);
        ++imported_count;
    } else if (key == "window-height") {
        config.window_height = std::stoi(value);
        ++imported_count;
    } else {
        // Unknown key
        warnings.push_back("Unknown Ghostty option: " + key);
        config.raw[key] = value;
        ++skipped_count;
    }
}

GhosttyImportResult GhosttyConfigImporter::import(
    const std::string& ghostty_config_path) {
    GhosttyImportResult result;

    std::ifstream file(ghostty_config_path);
    if (!file.is_open()) {
        result.warnings.push_back(
            "Could not open Ghostty config: " + ghostty_config_path);
        return result;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // First pass: find theme directive and apply as baseline,
    // so explicit color values can override the theme.
    {
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;
            auto eq = trimmed.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(trimmed.substr(0, eq));
            if (key == "theme") {
                std::string value = trim(trimmed.substr(eq + 1));
                result.config.theme = value;
                auto theme = findTheme(value);
                if (theme) {
                    applyTheme(result.config, *theme);
                } else {
                    importTheme(value, result.config, result.warnings);
                }
                break;
            }
        }
    }

    // Second pass: parse all directives.
    {
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            parseLine(line, result.config, result.warnings,
                      result.imported_count, result.skipped_count);
        }
    }

    return result;
}

std::string GhosttyConfigImporter::defaultGhosttyConfigPath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::string(appdata) + "\\ghostty\\config";
    }
    return "";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/ghostty/config";
    }
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/ghostty/config";
#endif
}

bool GhosttyConfigImporter::ghosttyConfigExists() {
    std::string path = defaultGhosttyConfigPath();
    if (path.empty()) return false;
    return fileExists(path);
}

std::vector<std::string> GhosttyConfigImporter::ghosttyThemeSearchPaths() {
    std::vector<std::string> paths;

#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        paths.push_back(std::string(appdata) + "\\ghostty\\themes");
    }
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        paths.push_back(std::string(xdg) + "/ghostty/themes");
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            paths.push_back(std::string(home) + "/.config/ghostty/themes");
        }
    }
    // System-installed themes
    paths.push_back("/usr/share/ghostty/themes");
    paths.push_back("/usr/local/share/ghostty/themes");
#endif

    return paths;
}

bool GhosttyConfigImporter::importTheme(const std::string& theme_name,
                                         Config& config,
                                         std::vector<std::string>& warnings) {
    auto search_paths = ghosttyThemeSearchPaths();

    for (const auto& dir : search_paths) {
        std::string theme_path = dir + "/" + theme_name;
        if (!fileExists(theme_path)) continue;

        // Ghostty theme files use the same key-value format with color defs.
        // Use the existing theme loader with Ghostty format detection.
        auto theme = loadThemeFile(theme_path, ThemeFormat::Ghostty);
        if (theme.ok()) {
            applyTheme(config, theme.value());
            return true;
        }

        // Fall back to manual parsing if loadThemeFile doesn't handle it.
        std::ifstream file(theme_path);
        if (!file.is_open()) continue;

        std::string line;
        bool found_any = false;
        while (std::getline(file, line)) {
            std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;
            auto eq = trimmed.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trim(trimmed.substr(0, eq));
            std::string value = trim(trimmed.substr(eq + 1));

            if (key == "background") {
                config.background = parseColor(value);
                found_any = true;
            } else if (key == "foreground") {
                config.foreground = parseColor(value);
                found_any = true;
            } else if (key == "cursor-color") {
                config.cursor_color = parseColor(value);
                found_any = true;
            } else if (key == "selection-background") {
                config.selection_background = parseColor(value);
                found_any = true;
            } else if (key == "selection-foreground") {
                config.selection_foreground = parseColor(value);
                found_any = true;
            } else if (key == "palette") {
                auto palette_eq = value.find('=');
                if (palette_eq != std::string::npos) {
                    int idx = std::stoi(value.substr(0, palette_eq));
                    if (idx >= 0 && idx < 16) {
                        config.palette[idx] =
                            parseColor(trim(value.substr(palette_eq + 1)));
                        found_any = true;
                    }
                }
            }
        }

        if (found_any) return true;
    }

    return false;
}

} // namespace termcore
