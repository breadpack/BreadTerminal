#ifndef TERMCORE_THEME_PREVIEW_H
#define TERMCORE_THEME_PREVIEW_H

#include "termcore/config.h"

#include <cstdint>
#include <optional>

namespace termcore {

/// Extracted preview colors from a theme for UI display.
struct PreviewColors {
    uint32_t bg = 0;
    uint32_t fg = 0xFFFFFF;
    uint32_t cursor = 0xFFFFFF;
    uint32_t selection_bg = 0x444444;
    uint32_t palette[16] = {};
};

/// Extract preview colors from a Theme.
PreviewColors previewColorsFromTheme(const Theme& theme);

/// Manages temporary theme preview state.
/// Allows applying a theme temporarily for preview, then reverting
/// to the original theme.
class ThemePreview {
public:
    /// Set the reference config that previews are relative to.
    void setBaseConfig(const Config& config);

    /// Temporarily apply a theme for preview.
    /// Stores the current config state so it can be reverted.
    void setPreviewTheme(Config& config, const Theme& theme);

    /// Revert the config to the state before preview.
    void revertPreview(Config& config);

    /// Check if currently in preview mode.
    bool isPreviewing() const;

    /// Get the currently previewed theme name, if any.
    std::optional<std::string> previewedThemeName() const;

private:
    bool previewing_ = false;
    Config saved_config_{};
    std::string preview_theme_name_;
};

} // namespace termcore

#endif
