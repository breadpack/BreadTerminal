#ifndef TERMCORE_THEME_INDEX_H
#define TERMCORE_THEME_INDEX_H

#include <string>
#include <vector>
#include <cstdint>

namespace termcore {

struct ThemeMetadata {
    std::string name;
    uint32_t background = 0;
    uint32_t foreground = 0xFFFFFF;
    uint32_t palette[16] = {};
    std::string source_url;
    bool is_dark = true;
    bool installed = false;
};

class ThemeIndex {
public:
    bool loadFromJSON(const std::string& json);
    const std::vector<ThemeMetadata>& all() const;
    std::vector<const ThemeMetadata*> search(const std::string& query) const;
    std::vector<const ThemeMetadata*> filterByCategory(bool dark_only, bool light_only, bool installed_only) const;
    void markInstalled(const std::string& name);
    void refreshInstallStatus(); // re-check defaultThemeDir
    size_t count() const;

private:
    std::vector<ThemeMetadata> themes_;
};

} // namespace termcore

#endif
