#ifndef TERMCORE_TERMINAL_CONTROLLER_H
#define TERMCORE_TERMINAL_CONTROLLER_H

#include "termcore/config.h"
#include "termcore/platform_host.h"
#include "termcore/font_manager.h"
#include "termcore/profile.h"
#include "termcore/selection_manager.h"
#include "termcore/search_controller.h"
#include "termcore/tab_controller.h"
#include "termcore/config_applier.h"
#include "termcore/keybinding.h"
#include "termcore/url_detector.h"
#include "termcore/url_highlight.h"
#include "termcore/paste_guard.h"
#include "termcore/vi_copy_mode.h"
#include "termcore/input_handler.h"
#include "termcore/clipboard_history.h"
#include "termcore/command_palette.h"
#include "termcore/profile_dropdown.h"
#include "termcore/lua_engine.h"
#include <chrono>
#include <memory>
#include <string>

namespace termcore {

class TerminalController {
public:
    TerminalController(IPlatformHost* host, Config config,
                       FontCollection* fontCollection);

    // --- Event entry points (platform calls these) ---
    void onKeyEvent(const KeyEvent& e);
    void onMouseEvent(const InputMouseEvent& e);
    void onCharInput(const std::string& utf8);
    void onResize(int pixelW, int pixelH);

    // Search (from search UI)
    void onSearchQuery(const std::string& query);
    void onSearchNext();
    void onSearchPrev();
    void onSearchHistoryPrev();
    void onSearchHistoryNext();

    // Config changes (from settings/hub UI)
    void onConfigChanged(const Config& newConfig);
    void onThemeChanged(const std::string& name);
    void onFontChanged(const std::string& family);

    // --- Lifecycle ---
    void initTerminal();
    void pollPty();
    void tick();  // cursor blink, resize overlay timeout

    // --- Accessors for renderer ---
    Screen* activeScreen();
    const Config& config() const { return config_; }
    const SelectionManager& selection() const { return selMgr_; }
    const SearchController& search() const { return searchCtrl_; }
    float cellWidth() const { return fontMgr_ ? fontMgr_->cellWidth() : 8.0f; }
    float cellHeight() const { return fontMgr_ ? fontMgr_->cellHeight() : 16.0f; }
    int termRows() const { return termRows_; }
    int termCols() const { return termCols_; }
    bool needsRender() const;
    void clearNeedsRender() { needsRender_ = false; }
    void flushPendingUrlScan();

    // Tab info
    std::vector<TabController::TabInfo> tabBarInfo() const;
    int tabCount() const;

    // URL detection
    const std::vector<DetectedUrl>& detectedUrls() const { return detectedUrls_; }

    // URL highlighting (clickable URLs with hover state and render hints)
    const UrlHighlightManager& urlHighlight() const { return urlHighlightMgr_; }
    UrlHighlightManager& urlHighlight() { return urlHighlightMgr_; }

    // Vi copy mode
    bool inCopyMode() const;

    // Clipboard history
    ClipboardHistory& clipboardHistory() { return clipboardHistory_; }
    const ClipboardHistory& clipboardHistory() const { return clipboardHistory_; }

    // Command palette
    CommandPalette& commandPalette() { return commandPalette_; }

    // Profile dropdown
    ProfileDropdown& profileDropdown() { return profileDropdown_; }

    // Direct access for platform-specific needs
    TabController* tabs() { return tabCtrl_.get(); }
    FontManager* fontMgr() { return fontMgr_.get(); }
    KeybindingManager* keybindings() { return keybindings_.get(); }
    ProfileManager* profileManager() { return profileMgr_.get(); }
    LuaEngine* luaEngine() { return luaEngine_.get(); }

    void pasteText(const std::string& text);

    // Broadcast input
    void broadcastWrite(const std::string& data);
    void toggleBroadcast();
    BroadcastMode broadcastMode() const;

private:
    void handleAction(Action action);
    void sendPtyData(const char* data, size_t len);

    IPlatformHost* host_;
    Config config_;

    std::unique_ptr<FontManager> fontMgr_;
    std::unique_ptr<ProfileManager> profileMgr_;
    SelectionManager selMgr_;
    SearchController searchCtrl_;
    std::unique_ptr<TabController> tabCtrl_;
    ConfigApplier configApplier_;
    std::unique_ptr<KeybindingManager> keybindings_;
    std::unique_ptr<ViCopyMode> copyMode_;

    UrlDetector urlDetector_;
    std::vector<DetectedUrl> detectedUrls_;
    UrlHighlightManager urlHighlightMgr_;

    std::unique_ptr<InputHandler> inputHandler_;
    std::unique_ptr<LuaEngine> luaEngine_;
    ClipboardHistory clipboardHistory_;
    CommandPalette commandPalette_;
    ProfileDropdown profileDropdown_;
    PasteGuard pasteGuard_;

    int termRows_ = 24;
    int termCols_ = 80;
    int lastPixelW_ = 0;
    int lastPixelH_ = 0;
    bool needsRender_ = false;
    bool urlScanPending_ = false;

    static constexpr float kTabBarHeightScale = 1.4f;
    bool isTabBarVisible() const;
    void recalcGrid();

    void initInputHandler();
    void saveSearchHistory();
};

} // namespace termcore
#endif
