#ifndef TERMCORE_THEME_IMPORTER_H
#define TERMCORE_THEME_IMPORTER_H

#include "termcore/config.h"

#include <optional>
#include <string>

namespace termcore {

/// Supported theme import formats.
enum class ThemeImportFormat {
    Auto,
    Ghostty,
    Kitty,
    WindowsTerminal,
    ITerm2,
    Alacritty,
};

/// Detects the format of a theme file from its content and/or extension.
ThemeImportFormat detectImportFormat(const std::string& content,
                                     const std::string& path);

/// Import a theme from a file, auto-detecting format.
std::optional<Theme> importFromFile(const std::string& path);

/// Import a theme from Ghostty format content.
std::optional<Theme> importFromGhostty(const std::string& content,
                                        const std::string& name = "Imported");

/// Import a theme from iTerm2 XML plist format.
std::optional<Theme> importFromITerm2(const std::string& xml,
                                       const std::string& name = "Imported");

/// Import a theme from Windows Terminal JSON format.
std::optional<Theme> importFromWindowsTerminal(const std::string& json,
                                                const std::string& name = "Imported");

/// Import a theme from Alacritty TOML/YAML format.
std::optional<Theme> importFromAlacritty(const std::string& content,
                                          const std::string& name = "Imported");

/// Import a theme from Kitty format content.
std::optional<Theme> importFromKitty(const std::string& content,
                                      const std::string& name = "Imported");

} // namespace termcore

#endif
