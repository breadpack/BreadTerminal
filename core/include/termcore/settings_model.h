#ifndef TERMCORE_SETTINGS_MODEL_H
#define TERMCORE_SETTINGS_MODEL_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

struct Config;

enum class SettingType { Toggle, Text, Number, Slider, Dropdown, ColorPicker };
enum class SectionType { Settings, CardGrid, KeybindingList };

/// Platform bitmask for settings visibility.
enum SettingPlatform : uint8_t {
    PlatformWindows = 1,
    PlatformMacOS   = 2,
    PlatformLinux   = 4,
    PlatformAll     = 7,
};

/// Returns the platform flag for the current build target.
inline uint8_t currentPlatform() {
#if defined(_WIN32)
    return PlatformWindows;
#elif defined(__APPLE__)
    return PlatformMacOS;
#elif defined(__linux__)
    return PlatformLinux;
#else
    return PlatformAll;
#endif
}

struct SettingMeta {
    float min = 0;
    float max = 0;
    float step = 1;
    std::vector<std::string> options;       // Dropdown stored values
    std::vector<std::string> option_labels; // Dropdown display labels (optional, same size as options)
};

struct SettingItem {
    std::string key;           // config field name
    std::string label;         // display name
    std::string description;   // help text
    SettingType type;
    SettingMeta meta;
    bool modified = false;
    uint8_t platforms = PlatformAll;  // bitmask of platforms where this setting is visible
};

struct SettingsCategory {
    std::string id;            // e.g. "appearance.theme"
    std::string label;         // e.g. "Theme"
    std::string parentId;      // e.g. "appearance" (empty for top-level)
    SectionType sectionType = SectionType::Settings;
    std::vector<SettingItem> items;
};

struct SettingsSearchMatch {
    std::string categoryId;
    std::string itemKey;
    size_t matchStart = 0;
    size_t matchLength = 0;
};

class SettingsModel {
public:
    SettingsModel(const Config& current, const Config& defaults);

    /// Returns categories where parentId is empty
    std::vector<const SettingsCategory*> topLevelCategories() const;

    /// Returns categories with the given parentId
    std::vector<const SettingsCategory*> subcategories(const std::string& parentId) const;

    /// Returns pointer to category by id, or nullptr
    const SettingsCategory* category(const std::string& id) const;

    /// Returns all categories
    const std::vector<SettingsCategory>& allCategories() const;

    /// Case-insensitive search across key, label, description
    std::vector<SettingsSearchMatch> search(const std::string& query) const;

    /// Re-check which items differ from defaults
    void refreshModified(const Config& current);

private:
    void buildCategories();
    void markModified(const Config& current);

    /// Helpers to get config values by key for comparison
    static std::string stringValue(const Config& cfg, const std::string& key);
    static float floatValue(const Config& cfg, const std::string& key);
    static int intValue(const Config& cfg, const std::string& key);
    static bool boolValue(const Config& cfg, const std::string& key);
    static uint32_t colorValue(const Config& cfg, const std::string& key);

    const Config& defaults_;
    std::vector<SettingsCategory> categories_;
};

} // namespace termcore

#endif
