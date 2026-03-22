#ifndef TERMCORE_THEME_REPOSITORY_H
#define TERMCORE_THEME_REPOSITORY_H

#include "termcore/config.h"

#include <string>
#include <vector>

namespace termcore {

/// Source classification for themes.
enum class ThemeSource {
    Builtin,
    User,
};

/// Extended theme info with source metadata.
struct ThemeInfo {
    Theme theme;
    ThemeSource source = ThemeSource::Builtin;
    bool is_dark = true;
};

/// Central repository for managing themes from all sources.
class ThemeRepository {
public:
    /// Get all built-in themes.
    std::vector<Theme> builtinThemes() const;

    /// Get user-installed custom themes from the user theme directory.
    std::vector<Theme> userThemes() const;

    /// Get all themes (built-in + user), sorted alphabetically.
    std::vector<Theme> allThemes() const;

    /// Get all themes with source metadata, sorted alphabetically.
    std::vector<ThemeInfo> allThemeInfos() const;

    /// Import a theme from a file (auto-detects format).
    /// Saves the imported theme to the user theme directory as JSON.
    /// Returns true on success.
    bool importTheme(const std::string& path);

    /// Export a theme to a file as JSON.
    /// Returns true on success.
    bool exportTheme(const std::string& name, const std::string& path) const;

    /// Delete a user-installed theme by name.
    /// Returns true if the theme was found and deleted.
    bool deleteUserTheme(const std::string& name);

    /// Get the platform-specific user theme directory path.
    std::string userThemeDirectory() const;

    /// Save a Theme to the user theme directory as JSON.
    /// Returns true on success.
    bool saveUserTheme(const Theme& theme) const;
};

} // namespace termcore

#endif
