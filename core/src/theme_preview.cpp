#include "termcore/theme_preview.h"

#include <cstring>

namespace termcore {

PreviewColors previewColorsFromTheme(const Theme& theme) {
    PreviewColors colors;
    colors.bg = theme.background;
    colors.fg = theme.foreground;
    colors.cursor = theme.cursor_color;
    colors.selection_bg = theme.selection_background;
    std::memcpy(colors.palette, theme.palette, sizeof(colors.palette));
    return colors;
}

void ThemePreview::setBaseConfig(const Config& config) {
    saved_config_ = config;
}

void ThemePreview::setPreviewTheme(Config& config, const Theme& theme) {
    if (!previewing_) {
        // Save current color state before first preview
        saved_config_ = config;
    }
    previewing_ = true;
    preview_theme_name_ = theme.name;
    applyTheme(config, theme);
}

void ThemePreview::revertPreview(Config& config) {
    if (!previewing_) return;

    // Restore only the color-related fields
    config.background = saved_config_.background;
    config.foreground = saved_config_.foreground;
    config.cursor_color = saved_config_.cursor_color;
    config.selection_background = saved_config_.selection_background;
    config.selection_foreground = saved_config_.selection_foreground;
    std::memcpy(config.palette, saved_config_.palette,
                sizeof(config.palette));

    previewing_ = false;
    preview_theme_name_.clear();
}

bool ThemePreview::isPreviewing() const {
    return previewing_;
}

std::optional<std::string> ThemePreview::previewedThemeName() const {
    if (!previewing_) return std::nullopt;
    return preview_theme_name_;
}

} // namespace termcore
