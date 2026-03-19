#ifndef TERMCORE_THEME_LOADER_H
#define TERMCORE_THEME_LOADER_H

#include "termcore/config.h"

#include <optional>
#include <string>
#include <vector>

namespace termcore {

enum class ThemeFormat { Auto, Ghostty, Kitty, WindowsTerminal };

/// Load a single theme file. Format=Auto detects from content/extension.
std::optional<Theme> loadThemeFile(const std::string& path,
                                   ThemeFormat format = ThemeFormat::Auto);

/// Parse theme from string content with explicit format.
std::optional<Theme> parseThemeString(const std::string& content,
                                      const std::string& name,
                                      ThemeFormat format);

/// Scan a directory for theme files. Returns all successfully parsed themes.
std::vector<Theme> scanThemeDirectory(const std::string& dir);

/// Get the user's theme directory path.
std::string defaultThemeDir();

/// Get ALL themes: built-in + user directory.
std::vector<Theme> allAvailableThemes();

/// Find a theme by name from all sources (built-in first, then user).
std::optional<Theme> findTheme(const std::string& name);

} // namespace termcore

#endif
