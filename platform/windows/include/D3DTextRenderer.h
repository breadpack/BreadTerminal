#ifndef TERMCORE_D3D_TEXT_RENDERER_H
#define TERMCORE_D3D_TEXT_RENDERER_H

#include "termcore/i_text_renderer.h"
#include "termcore/screen.h"
#include "termcore/mux.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/i_font_rasterizer.h"
#include <memory>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct ID3D11RenderTargetView;
struct ImeOverlay;
struct ScreenSnapshot;
#endif

namespace termcore {

/// GPU cell instance data -- must match layout in HLSL shaders.
struct D3DCellInstance {
    float position[2];     // Screen position (top-left of cell, in pixels)
    float atlas_uv[2];     // Top-left UV in atlas (in pixels)
    float atlas_size[2];   // Size of glyph in atlas (pixels)
    float glyph_offset[2]; // Bearing offset within cell
    float fg_color[4];     // Foreground color (RGBA, 0-1)
    float bg_color[4];     // Background color (RGBA, 0-1)
    uint32_t flags;        // Bit flags: bit0=has_glyph, bit1=is_color, bit2=is_bg, bit3=is_cursor, bit4=is_underline, bit5=is_rounded_rect_top
    uint32_t extra_flags;  // bits 0-2: underline_style; bits 3-4: render_mode (0=normal,1=box_drawing,2=block_element); bits 16-31: corner_radius * 16 (fixed-point)
};

/// D3D11-based terminal text renderer using instanced draw calls.
class D3DTextRenderer : public ITextRenderer {
public:
    D3DTextRenderer();
    ~D3DTextRenderer();

    D3DTextRenderer(const D3DTextRenderer&) = delete;
    D3DTextRenderer& operator=(const D3DTextRenderer&) = delete;

#if defined(_WIN32)
    /// Initialize D3D11 resources.
    bool initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    /// Set the render target view.
    void setRenderTarget(ID3D11RenderTargetView* rtv);
#endif

    /// Set the font collection and related objects for rendering.
    void setFontStack(FontCollection* collection,
                      GlyphCache* cache,
                      GlyphAtlas* atlas,
                      IFontRasterizer* rasterizer) override;

    /// Render a frame: read Screen data, build cell buffer, draw.
    void render(const Screen& screen) override;

    /// Two-phase rendering for lock-splitting:
    /// prepareFrame reads Screen data (needs synchronization),
    /// submitFrame does GPU work (no Screen access needed).
    void prepareFrame(const Screen& screen);
    void prepareFrame(const ::ScreenSnapshot& snap);
    void submitFrame();

    /// Handle viewport resize.
    void resize(float width, float height) override;

    /// Selection highlight state.
    struct Selection {
        int startRow = 0;
        int startCol = 0;
        int endRow = 0;
        int endCol = 0;
        bool active = false;
    };

    /// Set text selection for highlight rendering.
    void setSelection(const Selection& sel);

    /// Status bar information displayed at the bottom of the viewport.
    struct StatusBarInfo {
        std::string left_text;    // e.g., "master" (git branch)
        std::string center_text;  // e.g., ""
        std::string right_text;   // e.g., "80x24"
        uint32_t bg_color = 0x2d2d2d;  // dark gray
        uint32_t fg_color = 0xcccccc;  // light gray
        bool visible = true;
    };

    /// Set status bar content for rendering.
    void setStatusBar(const StatusBarInfo& info);

    /// Set resize overlay visibility and dimensions.
    void setResizeOverlay(bool visible, int cols, int rows);

    /// Set cursor blink visibility (called by blink timer).
    void setCursorBlink(bool visible);

    /// Hide cursor during IME composition.
    void setIMEActive(bool active);

    /// Mark content as dirty so next render does a full rebuild.
    /// Call this when screen content changes (e.g., after PTY output).
    void markContentDirty() override;

    /// Search highlight for a range of cells on a row.
    struct SearchHighlight {
        int row;
        int startCol;
        int endCol;       // exclusive
    };

    /// Set search highlights for rendering. currentIndex is the index into
    /// the highlights vector for the "current" match (rendered differently).
    void setSearchHighlights(const std::vector<SearchHighlight>& highlights,
                             int currentIndex);

    /// Tab information for the tab bar.
    struct TabInfo {
        std::string title;
        std::string icon_name;         // OSC 1 icon name
        std::string process_name;      // foreground process (for icon selection)
        bool active = false;
        bool has_unread = false;       // show dot indicator
        bool needs_attention = false;  // highlight tab background
        int agent_state = 0;           // AgentState as int (avoids core dependency)
        float progress_value = -1.0f;  // -1 = hidden, 0.0-1.0 = percentage
    };

    /// Tab bar height multiplier relative to cell height.
    static constexpr float kTabBarHeightScale = 1.4f;

    /// Tab bar displayed at the top of the viewport.
    struct TabBarInfo {
        std::vector<TabInfo> tabs;
        uint32_t bg_color = 0x1e1e1e;         // tab bar background (darker)
        uint32_t active_bg_color = 0x2d2d2d;   // active tab = terminal bg
        uint32_t inactive_bg_color = 0x1e1e1e; // inactive tab background
        uint32_t fg_color = 0xcccccc;          // text color
        uint32_t accent_color = 0x007acc;      // accent for indicator
        int hovered_tab = -1;                  // tab under mouse (-1 = none)
        bool hover_close = false;              // mouse over close button area
        bool hover_plus = false;               // mouse over "+" button
        bool visible = false;
        float height_scale = kTabBarHeightScale; // configurable height multiplier
        // Process name -> icon codepoint hex string (from config)
        const std::unordered_map<std::string, std::string>* process_icon_map = nullptr;
    };

    /// Set tab bar content for rendering.
    void setTabBar(const TabBarInfo& info);
    /// Get current tab bar state (for hover updates).
    TabBarInfo getTabBar() const;

    /// A single border segment between panes.
    struct BorderSegment {
        float x;       // start x in pixels
        float y;       // start y in pixels
        float width;   // width in pixels (1 for vertical borders)
        float height;  // height in pixels (1 for horizontal borders)
        bool active;   // true if adjacent to active pane
        bool needs_attention = false;  // glow border when agent needs input
        bool has_unread = false;       // subtle dot when unread notifications
        float ring_intensity = 0.0f;   // 0.0-1.0 pulse animation alpha
        uint32_t ring_color = 0x007acc; // glow color (blue default, orange critical)
    };

    /// Pane border information for split pane rendering.
    struct PaneBorderInfo {
        std::vector<BorderSegment> segments;
        uint32_t active_color = 0x007acc;    // accent blue
        uint32_t inactive_color = 0x3c3c3c;  // dark gray
        bool visible = false;
    };

    /// Set pane border segments for rendering.
    void setPaneBorders(const PaneBorderInfo& info);

    /// Sidebar subagent entry for tree display.
    struct SidebarSubagentEntry {
        std::string name;
        std::string status;     // "[Running]", "[Done]" etc.
        int state = 0;          // AgentState as int
        int indent_level = 0;
    };

    /// A single pane entry in the sidebar.
    struct SidebarRenderEntry {
        uint32_t pane_id = 0;
        std::string title;
        std::string subtitle;        // "main . PR #42"
        std::string status_text;     // "Thinking... (8s)"
        int agent_state = 0;
        float progress_value = -1.0f;
        std::string progress_label;
        bool has_unread = false;
        bool active = false;
        float attention_intensity = 0.0f;
        std::vector<SidebarSubagentEntry> subagents;
        bool subagents_expanded = true;
    };

    /// Sidebar panel render information.
    struct SidebarRenderInfo {
        bool visible = false;
        int width = 220;
        std::vector<SidebarRenderEntry> entries;
        int hovered_entry = -1;
        int hovered_subagent = -1;
        int scroll_offset = 0;
        uint32_t bg_color = 0x1a1a1a;
        uint32_t fg_color = 0xcccccc;
        uint32_t accent_color = 0x007acc;
        uint32_t separator_color = 0x333333;
        // Configurable state colors
        uint32_t color_running = 0xEAB308;
        uint32_t color_thinking = 0xEAB308;
        uint32_t color_tool_use = 0xF97316;
        uint32_t color_waiting = 0x3B82F6;
        uint32_t color_error = 0xEF4444;
        uint32_t color_idle = 0x22C55E;
    };

    /// Set sidebar content for rendering.
    void setSidebar(const SidebarRenderInfo& info);

    /// Return the atlas uploader for GPU texture management.
    IAtlasUploader* atlasUploader() override;

    /// Per-pane progress bar information.
    struct PaneProgressInfo {
        float progress = -1.0f;   // -1 = hidden, 0.0-1.0 = percentage
        std::string label;
        uint32_t color = 0x007acc; // accent blue
        float bar_height = 2.0f;          // configurable bar height
        uint32_t track_color = 0x1e1e1e;  // configurable track color
    };

    /// Set progress bar for a specific pane.
    void setPaneProgress(PaneId pane_id, const PaneProgressInfo& info);

    /// A status pill displayed in the status bar area.
    struct StatusPillInfo {
        std::string text;         // "key: value"
        uint32_t bg_color = 0x007acc;
        uint32_t fg_color = 0xffffff;
    };

    /// Set status pills for a specific pane.
    void setPaneStatusPills(PaneId pane_id, const std::vector<StatusPillInfo>& pills);

    /// Command palette overlay.
    struct CommandPaletteInfo {
        bool visible = false;
        std::string query;                  // current input text
        int selectedIndex = 0;              // highlighted item
        struct Item {
            std::string name;
            std::string shortcut_hint;
        };
        std::vector<Item> items;            // filtered items to display
        uint32_t bg_color = 0x252526;       // palette background
        uint32_t input_bg_color = 0x3c3c3c; // input field background
        uint32_t fg_color = 0xcccccc;       // text color
        uint32_t selected_bg = 0x094771;    // selected item background
        uint32_t selected_fg = 0xffffff;    // selected item text
        uint32_t hint_fg = 0x808080;        // shortcut hint color
        // Configurable layout
        float width_percent = 0.6f;         // width as fraction of viewport
        int max_items = 10;                 // maximum visible items
        float backdrop_opacity = 0.4f;      // dimming overlay opacity
    };

    /// Set command palette state for rendering.
    void setCommandPalette(const CommandPaletteInfo& info);

    /// Profile dropdown overlay.
    struct ProfileDropdownInfo {
        bool visible = false;
        int selectedIndex = 0;
        struct Item {
            std::string name;
            std::string icon;
        };
        std::vector<Item> items;
        uint32_t bg_color = 0x252526;
        uint32_t fg_color = 0xcccccc;
        uint32_t selected_bg = 0x094771;
        uint32_t selected_fg = 0xffffff;
        uint32_t hint_fg = 0x808080;
    };

    /// Set profile dropdown state for rendering.
    void setProfileDropdown(const ProfileDropdownInfo& info);

    /// URL highlight for a range of cells on a row.
    struct UrlHighlight {
        int row;
        int startCol;
        int endCol;       // exclusive
        bool hovered;
        uint32_t color;   // RGB color for the underline
    };

    /// Set URL highlights for rendering.
    void setUrlHighlights(const std::vector<UrlHighlight>& highlights);

    /// Set background opacity (0.0-1.0). Only affects background cells and clear color.
    /// Text and foreground elements remain fully opaque.
    void setBackgroundOpacity(float opacity);

    /// Set ghost text (dim suggestion) to display at the given row/col.
    /// Pass an empty string or row=-1 to clear.
    void setGhostText(const std::string& text, int row, int col);

    /// Enable or disable font ligatures in the cell builder.
    void setFontLigatures(bool enabled);

    /// Set IME composition overlay for virtual rendering without mutating Screen cells.
    /// The overlay is applied during cell buffer construction in the renderer.
    void setImeOverlay(const struct ImeOverlay& overlay);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore
#endif
