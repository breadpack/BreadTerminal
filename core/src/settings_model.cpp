#include "termcore/settings_model.h"
#include "termcore/config.h"

#include <algorithm>
#include <cctype>

namespace termcore {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string toLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

static size_t findInsensitive(const std::string& haystack, const std::string& needle) {
    std::string h = toLower(haystack);
    std::string n = toLower(needle);
    return h.find(n);
}

// ---------------------------------------------------------------------------
// Config value accessors by key
// ---------------------------------------------------------------------------

std::string SettingsModel::stringValue(const Config& cfg, const std::string& key) {
    if (key == "shell") return cfg.shell;
    if (key == "cursor_style") return cfg.cursor_style;
    if (key == "clipboard_paste_protection") return cfg.clipboard_paste_protection;
    if (key == "font_family") return cfg.font_family;
    if (key == "theme") return cfg.theme;
    return {};
}

float SettingsModel::floatValue(const Config& cfg, const std::string& key) {
    if (key == "font_size") return cfg.font_size;
    if (key == "background_opacity") return cfg.background_opacity;
    if (key == "cursor_blink_interval") return cfg.cursor_blink_interval;
    if (key == "minimum_contrast") return cfg.minimum_contrast;
    return 0.0f;
}

int SettingsModel::intValue(const Config& cfg, const std::string& key) {
    if (key == "window_width") return cfg.window_width;
    if (key == "window_height") return cfg.window_height;
    if (key == "window_padding") return cfg.window_padding;
    if (key == "scrollback_limit") return cfg.scrollback_limit;
    if (key == "background_blur") return cfg.background_blur;
    return 0;
}

bool SettingsModel::boolValue(const Config& cfg, const std::string& key) {
    if (key == "cursor_blink") return cfg.cursor_blink;
    if (key == "clipboard_paste_bracketed_safe") return cfg.clipboard_paste_bracketed_safe;
    if (key == "allow_clipboard_write") return cfg.allow_clipboard_write;
    return false;
}

uint32_t SettingsModel::colorValue(const Config& cfg, const std::string& key) {
    if (key == "background") return cfg.background;
    if (key == "foreground") return cfg.foreground;
    if (key == "cursor_color") return cfg.cursor_color;
    if (key == "selection_background") return cfg.selection_background;
    if (key == "selection_foreground") return cfg.selection_foreground;
    return 0;
}

// ---------------------------------------------------------------------------
// Modified detection
// ---------------------------------------------------------------------------

static bool isStringKey(const std::string& key) {
    return key == "shell" || key == "cursor_style" ||
           key == "clipboard_paste_protection" || key == "font_family" ||
           key == "theme";
}

static bool isFloatKey(const std::string& key) {
    return key == "font_size" || key == "background_opacity" ||
           key == "cursor_blink_interval" || key == "minimum_contrast";
}

static bool isIntKey(const std::string& key) {
    return key == "window_width" || key == "window_height" ||
           key == "window_padding" || key == "scrollback_limit" ||
           key == "background_blur";
}

static bool isBoolKey(const std::string& key) {
    return key == "cursor_blink" || key == "clipboard_paste_bracketed_safe" ||
           key == "allow_clipboard_write";
}

static bool isColorKey(const std::string& key) {
    return key == "background" || key == "foreground" ||
           key == "cursor_color" || key == "selection_background" ||
           key == "selection_foreground";
}

void SettingsModel::markModified(const Config& current) {
    for (auto& cat : categories_) {
        for (auto& item : cat.items) {
            const auto& k = item.key;
            if (isStringKey(k))
                item.modified = stringValue(current, k) != stringValue(defaults_, k);
            else if (isFloatKey(k))
                item.modified = floatValue(current, k) != floatValue(defaults_, k);
            else if (isIntKey(k))
                item.modified = intValue(current, k) != intValue(defaults_, k);
            else if (isBoolKey(k))
                item.modified = boolValue(current, k) != boolValue(defaults_, k);
            else if (isColorKey(k))
                item.modified = colorValue(current, k) != colorValue(defaults_, k);
        }
    }
}

// ---------------------------------------------------------------------------
// Category tree builder
// ---------------------------------------------------------------------------

void SettingsModel::buildCategories() {
    categories_.clear();

    // -- General --
    categories_.push_back({"general", "General", "", SectionType::Settings, {}});

    categories_.push_back({"general.shell", "Shell", "general", SectionType::Settings, {
        {"shell", "Shell", "Path to shell executable. Empty uses $SHELL.", SettingType::Text, {}, false},
    }});

    categories_.push_back({"general.window", "Window", "general", SectionType::Settings, {
        {"window_width", "Window Width", "Default window width in pixels.", SettingType::Number, {}, false},
        {"window_height", "Window Height", "Default window height in pixels.", SettingType::Number, {}, false},
        {"window_padding", "Window Padding", "Padding around the terminal content in pixels.", SettingType::Number, {}, false},
    }});

    categories_.push_back({"general.scrollback", "Scrollback", "general", SectionType::Settings, {
        {"scrollback_limit", "Scrollback Limit", "Maximum number of lines kept in scrollback buffer.", SettingType::Number, {}, false},
    }});

    // -- Appearance --
    categories_.push_back({"appearance", "Appearance", "", SectionType::Settings, {}});

    categories_.push_back({"appearance.theme", "Theme", "appearance", SectionType::CardGrid, {}});

    categories_.push_back({"appearance.opacity", "Opacity & Blur", "appearance", SectionType::Settings, {
        {"background_opacity", "Background Opacity", "Window background opacity (0 = transparent, 1 = opaque).",
         SettingType::Slider, {0.0f, 1.0f, 0.01f, {}}, false},
        {"background_blur", "Background Blur", "Blur level applied behind the window.",
         SettingType::Dropdown, {0, 0, 0, {"None", "Low", "Medium", "High"}}, false},
    }});

    categories_.push_back({"appearance.cursor", "Cursor", "appearance", SectionType::Settings, {
        {"cursor_style", "Cursor Style", "Shape of the terminal cursor.",
         SettingType::Dropdown, {0, 0, 0, {"block", "underline", "bar"}}, false},
        {"cursor_blink", "Cursor Blink", "Whether the cursor blinks.", SettingType::Toggle, {}, false},
        {"cursor_blink_interval", "Blink Interval", "Cursor blink interval in seconds.",
         SettingType::Slider, {0.1f, 2.0f, 0.1f, {}}, false},
    }});

    categories_.push_back({"appearance.colors", "Colors", "appearance", SectionType::Settings, {
        {"background", "Background", "Terminal background color.", SettingType::ColorPicker, {}, false},
        {"foreground", "Foreground", "Terminal text color.", SettingType::ColorPicker, {}, false},
        {"cursor_color", "Cursor Color", "Color of the cursor.", SettingType::ColorPicker, {}, false},
        {"selection_background", "Selection Background", "Background color for selected text.", SettingType::ColorPicker, {}, false},
        {"selection_foreground", "Selection Foreground", "Text color for selected text.", SettingType::ColorPicker, {}, false},
        {"minimum_contrast", "Minimum Contrast", "WCAG 2.0 minimum contrast ratio (1.0 = disabled).",
         SettingType::Slider, {1.0f, 21.0f, 0.5f, {}}, false},
    }});

    // -- Font --
    categories_.push_back({"font", "Font", "", SectionType::Settings, {}});

    categories_.push_back({"font.family", "Font Family", "font", SectionType::CardGrid, {}});

    categories_.push_back({"font.size", "Font Size & Features", "font", SectionType::Settings, {
        {"font_size", "Font Size", "Size of the terminal font in points.",
         SettingType::Number, {6.0f, 72.0f, 0.5f, {}}, false},
    }});

    // -- Keyboard --
    categories_.push_back({"keyboard", "Keyboard", "", SectionType::Settings, {}});

    categories_.push_back({"keyboard.bindings", "Keybindings", "keyboard", SectionType::KeybindingList, {}});

    // -- Clipboard --
    categories_.push_back({"clipboard", "Clipboard", "", SectionType::Settings, {}});

    categories_.push_back({"clipboard.paste", "Paste Protection", "clipboard", SectionType::Settings, {
        {"clipboard_paste_protection", "Paste Protection", "When to show paste confirmation dialog.",
         SettingType::Dropdown, {0, 0, 0, {"never", "multiline", "always"}}, false},
        {"clipboard_paste_bracketed_safe", "Bracketed Paste Safe", "Filter potentially dangerous escape sequences from pasted text.",
         SettingType::Toggle, {}, false},
    }});

    categories_.push_back({"clipboard.permissions", "Permissions", "clipboard", SectionType::Settings, {
        {"allow_clipboard_write", "Allow Clipboard Write", "Allow applications to write to the system clipboard via OSC 52.",
         SettingType::Toggle, {}, false},
    }});
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SettingsModel::SettingsModel(const Config& current, const Config& defaults)
    : defaults_(defaults) {
    buildCategories();
    markModified(current);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<const SettingsCategory*> SettingsModel::topLevelCategories() const {
    std::vector<const SettingsCategory*> result;
    for (const auto& cat : categories_) {
        if (cat.parentId.empty())
            result.push_back(&cat);
    }
    return result;
}

std::vector<const SettingsCategory*> SettingsModel::subcategories(const std::string& parentId) const {
    std::vector<const SettingsCategory*> result;
    for (const auto& cat : categories_) {
        if (cat.parentId == parentId)
            result.push_back(&cat);
    }
    return result;
}

const SettingsCategory* SettingsModel::category(const std::string& id) const {
    for (const auto& cat : categories_) {
        if (cat.id == id)
            return &cat;
    }
    return nullptr;
}

const std::vector<SettingsCategory>& SettingsModel::allCategories() const {
    return categories_;
}

std::vector<SearchMatch> SettingsModel::search(const std::string& query) const {
    std::vector<SearchMatch> results;
    if (query.empty()) return results;

    for (const auto& cat : categories_) {
        for (const auto& item : cat.items) {
            // Search in key
            size_t pos = findInsensitive(item.key, query);
            if (pos != std::string::npos) {
                results.push_back({cat.id, item.key, pos, query.size()});
                continue;
            }
            // Search in label
            pos = findInsensitive(item.label, query);
            if (pos != std::string::npos) {
                results.push_back({cat.id, item.key, pos, query.size()});
                continue;
            }
            // Search in description
            pos = findInsensitive(item.description, query);
            if (pos != std::string::npos) {
                results.push_back({cat.id, item.key, pos, query.size()});
            }
        }
    }
    return results;
}

void SettingsModel::refreshModified(const Config& current) {
    markModified(current);
}

} // namespace termcore
