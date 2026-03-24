#include "termcore/settings_model.h"
#include "termcore/config.h"
#include "termcore/config_field_registry.h"
#include "termcore/profile.h"

#include <algorithm>
#include <cmath>
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
// Config value accessors by key — delegated to field registry
// ---------------------------------------------------------------------------

std::string SettingsModel::stringValue(const Config& cfg, const std::string& key) {
    if (auto* f = findStringField(key)) return cfg.*f->member;
    return {};
}

float SettingsModel::floatValue(const Config& cfg, const std::string& key) {
    if (auto* f = findFloatField(key)) return cfg.*f->member;
    return 0.0f;
}

int SettingsModel::intValue(const Config& cfg, const std::string& key) {
    if (auto* f = findIntField(key)) return cfg.*f->member;
    return 0;
}

bool SettingsModel::boolValue(const Config& cfg, const std::string& key) {
    if (auto* f = findBoolField(key)) return cfg.*f->member;
    return false;
}

uint32_t SettingsModel::colorValue(const Config& cfg, const std::string& key) {
    if (auto* f = findColorField(key)) return cfg.*f->member;
    return 0;
}

// ---------------------------------------------------------------------------
// Modified detection — uses registry lookup instead of hardcoded key lists
// ---------------------------------------------------------------------------

void SettingsModel::markModified(const Config& current) {
    for (auto& cat : categories_) {
        for (auto& item : cat.items) {
            const auto& k = item.key;
            if (findStringField(k))
                item.modified = stringValue(current, k) != stringValue(defaults_, k);
            else if (findFloatField(k))
                item.modified = std::abs(floatValue(current, k) - floatValue(defaults_, k)) > 0.001f;
            else if (findIntField(k))
                item.modified = intValue(current, k) != intValue(defaults_, k);
            else if (findBoolField(k))
                item.modified = boolValue(current, k) != boolValue(defaults_, k);
            else if (findColorField(k))
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

    {
        auto detected = ShellDetector::detect();
        std::vector<std::string> shellValues = {""};
        std::vector<std::string> shellLabels = {"Default"};
        for (const auto& p : detected) {
            shellValues.push_back(p.command);
            shellLabels.push_back(p.name);
        }
        categories_.push_back({"general.shell", "Shell", "general", SectionType::Settings, {
            {"shell", "Shell", "Shell program to use. Default uses system shell.",
             SettingType::Dropdown, {0, 0, 0, std::move(shellValues), std::move(shellLabels)}, false},
        }});
    }

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
         SettingType::Slider, {0.0f, 1.0f, 0.01f, {}}, false, PlatformAll},
        {"background_blur_mode", "Blur Mode", "None: no blur. Acrylic: DWM-managed desktop blur (Windows).",
         SettingType::Dropdown, {0, 0, 0, {"none", "acrylic"}}, false, PlatformWindows},
        {"background_blur_material", "Blur Material", "macOS visual effect material for window background blur.",
         SettingType::Dropdown, {0, 0, 0, {"none", "hud_window", "sheet", "under_window"}}, false, PlatformMacOS},
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

    categories_.push_back({"keyboard.preset", "Preset", "keyboard", SectionType::Settings, {
        {"keybinding_preset", "Keybinding Preset", "Load keybindings from another terminal. Your custom keymaps are applied on top.",
         SettingType::Dropdown, {0, 0, 0, {"Default", "Ghostty", "Kitty", "tmux", "Warp", "Windows Terminal", "Alacritty", "iTerm2"}}, false},
    }});

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

    // -- Profiles --
    categories_.push_back({"profiles", "Profiles", "", SectionType::Settings, {}});
    categories_.push_back({"profiles.all", "All Profiles", "profiles", SectionType::CardGrid, {}});
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

std::vector<SettingsSearchMatch> SettingsModel::search(const std::string& query) const {
    std::vector<SettingsSearchMatch> results;
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

void SettingsModel::addLuaCategory(const std::string& name,
                                   const std::vector<SettingItem>& items) {
    // Ensure a top-level "plugins" parent exists (created once).
    static const std::string kPluginsId = "plugins";
    bool has_plugins_root = false;
    for (const auto& cat : categories_) {
        if (cat.id == kPluginsId) { has_plugins_root = true; break; }
    }
    if (!has_plugins_root) {
        categories_.push_back({kPluginsId, "Plugins", "", SectionType::Settings, {}});
    }

    // Derive a stable id from the category name.
    std::string id = kPluginsId + "." + name;
    // Replace spaces with underscores for safe key.
    for (char& c : id) { if (c == ' ') c = '_'; }

    // Avoid duplicate category ids.
    for (const auto& cat : categories_) {
        if (cat.id == id) return;
    }

    categories_.push_back({id, name, kPluginsId, SectionType::Settings, items});
}

void SettingsModel::clearLuaCategories() {
    static const std::string kPluginsId = "plugins";
    // Remove all categories whose parentId is "plugins" (or the root itself).
    categories_.erase(
        std::remove_if(categories_.begin(), categories_.end(),
            [&](const SettingsCategory& cat) {
                return cat.id == kPluginsId || cat.parentId == kPluginsId;
            }),
        categories_.end());
}

} // namespace termcore
