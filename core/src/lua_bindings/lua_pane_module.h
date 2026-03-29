// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_pane_module.h
#pragma once

#include "termcore/lua_module.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

// ---- Data structures for pane info and overlays ----

struct PaneInfo {
    uint32_t id = 0;
    int rows = 0;
    int cols = 0;
    std::string title;
    std::string cwd;
    std::string process;
    bool is_active = false;
};

struct VirtualTextEntry {
    uint64_t id = 0;
    int row = 0;
    int col = 0;
    std::string text;
    uint32_t fg = 0;
    uint32_t bg = 0;  // 0 = transparent
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

struct HighlightEntry {
    uint64_t id = 0;
    int row = 0;
    int start_col = 0;
    int end_col = 0;
    uint32_t fg = 0;
    uint32_t bg = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
};

struct LineSign {
    std::string text;  // 2 chars max
    uint32_t color = 0;
};

// ---- Provider interface for host pane data ----

class IPaneDataProvider {
public:
    virtual ~IPaneDataProvider() = default;
    virtual std::vector<PaneInfo> listPanes() const = 0;
    virtual PaneInfo getActivePane() const = 0;
    virtual std::vector<std::string> getPaneLines(uint32_t pane_id, int start, int end) const = 0;
    virtual std::string getPaneLine(uint32_t pane_id, int row) const = 0;
    virtual std::pair<int, int> getPaneCursor(uint32_t pane_id) const = 0;
    virtual std::string getPaneSelection(uint32_t pane_id) const = 0;
    virtual std::pair<int, int> getSelectionStart(uint32_t pane_id) const = 0;
    virtual std::pair<int, int> getSelectionEnd(uint32_t pane_id) const = 0;
    virtual int getScrollbackSize(uint32_t pane_id) const = 0;
    virtual std::vector<std::string> getScrollbackLines(uint32_t pane_id, int start, int count) const = 0;
    virtual void sendText(uint32_t pane_id, const std::string& text) = 0;
    virtual void sendKeys(uint32_t pane_id, const std::string& keys) = 0;
};

// ---- Forward declaration for the Lua handle ----
struct LuaPaneHandle;

// ---- Module ----

class LuaPaneModule : public ILuaModule {
public:
    LuaPaneModule();

    std::string_view moduleName() const override { return "pane"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::PaneRead;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    void setProvider(IPaneDataProvider* provider);
    IPaneDataProvider* provider() const { return provider_; }

    // --- Host queries for rendering overlays ---
    const std::vector<VirtualTextEntry>& virtualTexts(uint32_t pane_id) const;
    const std::vector<HighlightEntry>& highlights(uint32_t pane_id) const;
    const std::unordered_map<int, LineSign>& lineSigns(uint32_t pane_id) const;

    // --- Host calls to dispatch events ---
    void fireOutput(uint32_t pane_id, const std::string& text);
    void fireExit(uint32_t pane_id, int exit_code);

    // --- Overlay mutation (called from LuaPaneHandle) ---
    uint64_t addVirtualText(uint32_t pane_id, int row, int col,
                            const std::string& text, uint32_t fg, uint32_t bg,
                            bool bold, bool italic, bool underline);
    bool removeVirtualText(uint32_t pane_id, uint64_t mark_id);
    void clearVirtualText(uint32_t pane_id);

    uint64_t addHighlight(uint32_t pane_id, int row, int start_col, int end_col,
                          uint32_t fg, uint32_t bg,
                          bool bold, bool italic, bool underline);
    bool removeHighlight(uint32_t pane_id, uint64_t mark_id);
    void clearHighlights(uint32_t pane_id);

    void setLineSign(uint32_t pane_id, int row, const std::string& text, uint32_t color);
    void removeLineSign(uint32_t pane_id, int row);

    // --- Callback registration (called from LuaPaneHandle) ---
    void addOnOutput(uint32_t pane_id, std::shared_ptr<void> fn);
    void addOnExit(uint32_t pane_id, std::shared_ptr<void> fn);

private:
    IPaneDataProvider* provider_ = nullptr;

    // Per-pane overlay storage
    struct PaneOverlays {
        std::vector<VirtualTextEntry> virtual_texts;
        std::vector<HighlightEntry> highlights;
        std::unordered_map<int, LineSign> line_signs;
    };
    std::unordered_map<uint32_t, PaneOverlays> overlays_;
    uint64_t next_mark_id_ = 1;

    // Per-pane event callbacks (stored as void* to avoid sol in header)
    struct PaneCallbacks {
        std::vector<std::shared_ptr<void>> on_output;
        std::vector<std::shared_ptr<void>> on_exit;
    };
    std::unordered_map<uint32_t, PaneCallbacks> callbacks_;

    // Empty containers for const-ref returns
    static const std::vector<VirtualTextEntry> empty_virtual_texts_;
    static const std::vector<HighlightEntry> empty_highlights_;
    static const std::unordered_map<int, LineSign> empty_line_signs_;
};

} // namespace termcore
