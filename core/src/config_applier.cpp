#include "termcore/config_applier.h"
#include "termcore/tab_controller.h"
#include "termcore/font_manager.h"
#include "termcore/lua_config.h"

namespace termcore {

void ConfigApplier::applyFull(Config& config, const Config& newConfig,
                              TabController& tabs, FontManager& fontMgr,
                              IPlatformHost* host)
{
    config = newConfig;

    // Apply theme if set
    if (!config.theme.empty()) {
        auto* theme = getBuiltinTheme(config.theme);
        if (theme) applyTheme(config, *theme);
    }

    // Update font
    fontMgr.setFont(config.font_family, config.font_size);
    if (host) {
        host->onFontChanged(fontMgr.cellWidth(), fontMgr.cellHeight());
    }

    // Recalculate grid and resize all panes
    if (host) {
        int vpW = 0, vpH = 0;
        host->getViewportSize(vpW, vpH);
        int rows = 0, cols = 0;
        fontMgr.recalcGrid(vpW, vpH, rows, cols);
        tabs.resizeAllPanes(rows, cols);
        host->onGridSizeChanged(rows, cols);
    }

    // Update colors on all screens
    tabs.forEachPane([&](PaneState& ps) {
        if (ps.screen) {
            ps.screen->initDynamicColors(config);
        }
    });

    if (host) {
        host->onColorsChanged();
        host->invalidate();
    }
}

void ConfigApplier::applyColors(Config& config, const Config& newConfig,
                                TabController& tabs, IPlatformHost* host)
{
    // Copy color fields
    config.background = newConfig.background;
    config.foreground = newConfig.foreground;
    config.cursor_color = newConfig.cursor_color;
    config.selection_background = newConfig.selection_background;
    config.selection_foreground = newConfig.selection_foreground;
    config.theme = newConfig.theme;
    for (int i = 0; i < 16; ++i) {
        config.palette[i] = newConfig.palette[i];
    }

    tabs.forEachPane([&](PaneState& ps) {
        if (ps.screen) {
            ps.screen->initDynamicColors(config);
        }
    });

    if (host) {
        host->onColorsChanged();
        host->invalidate();
    }
}

void ConfigApplier::applyFont(Config& config, const std::string& family,
                              TabController& tabs, FontManager& fontMgr,
                              IPlatformHost* host)
{
    config.font_family = family;
    fontMgr.setFont(family, fontMgr.currentFontSize());

    if (host) {
        host->onFontChanged(fontMgr.cellWidth(), fontMgr.cellHeight());

        int vpW = 0, vpH = 0;
        host->getViewportSize(vpW, vpH);
        int rows = 0, cols = 0;
        fontMgr.recalcGrid(vpW, vpH, rows, cols);
        tabs.resizeAllPanes(rows, cols);
        host->onGridSizeChanged(rows, cols);
        host->invalidate();
    }
}

void ConfigApplier::persist(const Config& config) {
    std::string luaPath = luaConfigWritePath();
    if (!luaPath.empty()) {
        writeConfigLua(luaPath, config);
    }
}

} // namespace termcore
