// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_window_module.h
#pragma once

#include "termcore/lua_module.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// Visual style for a floating window (renderer-friendly, no sol types).
struct FloatingWindowStyle {
    uint32_t background = 0x1e1e2e;
    uint32_t foreground = 0xcdd6f4;
    uint32_t border_color = 0x89b4fa;
    uint32_t title_color = 0xf5e0dc;
};

/// A highlighted range inside a floating window.
struct HighlightRange {
    int row = 0;
    int start_col = 0;
    int end_col = 0;
    uint32_t fg = 0xffffff;
    uint32_t bg = 0x000000;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

/// Complete state for one floating window.
/// All fields are plain C++ types so the renderer can read them without sol.
/// Callbacks are stored as shared_ptr<void> (actual type: sol::protected_function).
struct FloatingWindowData {
    uint64_t id = 0;
    bool open = true;
    int width = 60;
    int height = 20;
    int cursor_row = 0;
    int cursor_col = 0;
    int scroll_offset = 0;
    std::string title;
    std::string border_style = "rounded";
    std::string relative = "editor";
    std::string anchor = "center";
    bool focusable = true;

    FloatingWindowStyle style;
    std::vector<std::string> lines;
    std::vector<HighlightRange> highlights;

    // Callbacks stored as shared_ptr<void> to avoid sol.hpp in header.
    // Actual type: std::shared_ptr<sol::protected_function>
    std::unordered_map<std::string, std::shared_ptr<void>> key_handlers;
    std::shared_ptr<void> on_close_cb;
    std::shared_ptr<void> on_scroll_cb;
};

/// Lua module: terminal.window
class LuaWindowModule : public ILuaModule {
public:
    LuaWindowModule();

    std::string_view moduleName() const override { return "window"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::UI;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Host-facing API ---------------------------------------------------
    const std::vector<std::shared_ptr<FloatingWindowData>>& windows() const;
    void dispatchKey(uint64_t window_id, const std::string& key);
    void closeWindow(uint64_t window_id);
    bool hasOpenWindows() const;

    // Remove closed windows from memory. Call periodically from host.
    void removeClosedWindows();

    static constexpr size_t kMaxWindows = 64;

private:
    std::vector<std::shared_ptr<FloatingWindowData>> windows_;
    uint64_t next_id_ = 1;
};

} // namespace termcore
