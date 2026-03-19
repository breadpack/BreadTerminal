#ifndef TERMCORE_D3D_TEXT_RENDERER_H
#define TERMCORE_D3D_TEXT_RENDERER_H

#include "termcore/screen.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/i_font_rasterizer.h"
#include <memory>
#include <string>

#if defined(_WIN32)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct ID3D11RenderTargetView;
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
    uint32_t flags;        // Bit flags: bit0=has_glyph, bit1=is_color, bit2=is_bg, bit3=is_cursor, bit4=is_underline
    uint32_t extra_flags;  // bits 0-2: underline_style (0=none,1=single,2=double,3=curly,4=dotted,5=dashed)
};

/// D3D11-based terminal text renderer using instanced draw calls.
class D3DTextRenderer {
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
                      IFontRasterizer* rasterizer);

    /// Render a frame: read Screen data, build cell buffer, draw.
    void render(const Screen& screen);

    /// Handle viewport resize.
    void resize(float width, float height);

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
        bool active = false;
        bool has_unread = false;      // show dot indicator
        bool needs_attention = false;  // highlight tab background
    };

    /// Tab bar displayed at the top of the viewport.
    struct TabBarInfo {
        std::vector<TabInfo> tabs;
        uint32_t bg_color = 0x1e1e1e;         // tab bar background
        uint32_t active_bg_color = 0x2d2d2d;   // active tab background
        uint32_t inactive_bg_color = 0x1e1e1e; // inactive tab background
        uint32_t fg_color = 0xcccccc;          // text color
        bool visible = false;
    };

    /// Set tab bar content for rendering.
    void setTabBar(const TabBarInfo& info);

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore
#endif
