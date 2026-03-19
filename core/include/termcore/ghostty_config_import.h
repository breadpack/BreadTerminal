#ifndef TERMCORE_GHOSTTY_CONFIG_IMPORT_H
#define TERMCORE_GHOSTTY_CONFIG_IMPORT_H

#include "termcore/config.h"

#include <string>
#include <vector>

namespace termcore {

/// Result of importing a Ghostty config file.
struct GhosttyImportResult {
    Config config;
    std::vector<std::string> warnings;  // unsupported options
    int imported_count = 0;
    int skipped_count = 0;
};

/// Imports Ghostty terminal configuration files into BreadTerminal Config.
///
/// Supports the Ghostty key-value config format and maps known keys to
/// their BreadTerminal equivalents. Unknown keys are logged as warnings.
class GhosttyConfigImporter {
public:
    /// Import a Ghostty config file and return populated Config.
    GhosttyImportResult import(const std::string& ghostty_config_path);

    /// Get the default Ghostty config file path for the current platform.
    static std::string defaultGhosttyConfigPath();

    /// Check if a Ghostty config file exists at the default location.
    static bool ghosttyConfigExists();

    /// Import a Ghostty theme by name.
    /// Searches standard Ghostty theme directories for the theme file.
    /// Returns true if the theme was found and applied to the config.
    bool importTheme(const std::string& theme_name, Config& config,
                     std::vector<std::string>& warnings);

private:
    void parseLine(const std::string& line, Config& config,
                   std::vector<std::string>& warnings,
                   int& imported_count, int& skipped_count);
    uint32_t parseColor(const std::string& hex);
    void mapKeybind(const std::string& value, Config& config);

    /// Return list of directories to search for Ghostty themes.
    static std::vector<std::string> ghosttyThemeSearchPaths();
};

} // namespace termcore

#endif
