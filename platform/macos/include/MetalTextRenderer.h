#ifndef TERMCORE_METAL_TEXT_RENDERER_H
#define TERMCORE_METAL_TEXT_RENDERER_H

#include "termcore/i_text_renderer.h"
#include "termcore/screen.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/font_collection.h"
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __OBJC__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

namespace termcore {

/// GPU cell instance data -- Ghostty-style compact layout (32 bytes).
/// Must match CellInstance in cell.metal exactly.
struct alignas(16) CellInstance {
    uint16_t grid_col;      // Terminal column (0-based)
    uint16_t grid_row;      // Terminal row (0-based)
    uint16_t glyph_x;       // Atlas X position (pixels)
    uint16_t glyph_y;       // Atlas Y position (pixels)
    uint16_t glyph_width;   // Glyph width in atlas (pixels)
    uint16_t glyph_height;  // Glyph height in atlas (pixels)
    int16_t  offset_x;      // Bearing X offset from cell origin (signed pixels)
    int16_t  offset_y;      // Bearing Y offset: ascent - bearing_y (signed pixels)
    uint8_t  fg_r, fg_g, fg_b, fg_a;  // Foreground color
    uint8_t  bg_r, bg_g, bg_b, bg_a;  // Background color
    uint8_t  flags;          // bit0=has_glyph, bit1=is_color, bit2=is_bg_pass
    uint8_t  _pad[3];        // Pad to 32 bytes
};
static_assert(sizeof(CellInstance) == 32, "CellInstance must be exactly 32 bytes");

/// GPU uniform data -- must match Uniforms in cell.metal.
struct CellUniforms {
    float viewport_size[2];   // Physical pixels (drawableSize)
    float cell_size[2];       // Physical pixels (cell_width_px, cell_height_px)
    float atlas_size[2];      // Atlas texture dimensions (pixels)
    float grid_padding[2];    // Top-left padding offset (pixels), can be 0
};
static_assert(sizeof(CellUniforms) == 32, "CellUniforms must be exactly 32 bytes");

/// Selection state passed from the view to the renderer.
struct SelectionState {
    bool active = false;      // Is a selection in progress?
    bool block = false;       // Block (rectangular) selection mode?
    int start_row = 0;
    int start_col = 0;
    int end_row = 0;
    int end_col = 0;

    /// Normalize so start <= end (for line-based selection).
    void normalize(int& sr, int& sc, int& er, int& ec) const {
        sr = start_row; sc = start_col;
        er = end_row;   ec = end_col;
        if (sr > er || (sr == er && sc > ec)) {
            std::swap(sr, er);
            std::swap(sc, ec);
        }
    }

    /// Check if cell (row, col) is within the selection.
    bool contains(int row, int col) const {
        if (!active) return false;
        int sr, sc, er, ec;
        normalize(sr, sc, er, ec);
        if (block) {
            // Rectangular: each row uses same column range
            int minCol = std::min(sc, ec);
            int maxCol = std::max(sc, ec);
            // Re-compute row range without column normalization
            int minRow = std::min(start_row, end_row);
            int maxRow = std::max(start_row, end_row);
            return row >= minRow && row <= maxRow && col >= minCol && col <= maxCol;
        }
        // Line-based selection
        if (row < sr || row > er) return false;
        if (sr == er) return col >= sc && col <= ec;
        if (row == sr) return col >= sc;
        if (row == er) return col <= ec;
        return true; // middle rows are fully selected
    }
};

/// Metal-based terminal text renderer.
/// Reads Screen data and renders cells using instanced draw calls.
class MetalTextRenderer : public ITextRenderer {
public:
#ifdef __OBJC__
    MetalTextRenderer(id<MTLDevice> device, CAMetalLayer* layer);
#endif
    ~MetalTextRenderer();

    // Non-copyable
    MetalTextRenderer(const MetalTextRenderer&) = delete;
    MetalTextRenderer& operator=(const MetalTextRenderer&) = delete;

    /// Set the font collection and related objects for rendering.
    void setFontStack(FontCollection* collection,
                      GlyphCache* cache,
                      GlyphAtlas* atlas,
                      IFontRasterizer* rasterizer) override;

    /// Set the current selection state for rendering.
    void setSelection(const SelectionState& sel);

    /// Render a frame: read Screen data, build cell buffer, draw.
    void render(const Screen& screen) override;

    /// Handle viewport resize.
    void resize(float width, float height) override;

    /// Set background opacity for transparency support (0.0 - 1.0).
    void setBackgroundOpacity(float opacity);

    /// Set cursor blink interval in seconds (0.1 - 2.0).
    void setCursorBlinkInterval(float seconds);

    /// Hide cursor during IME composition.
    void setIMEActive(bool active);

    /// Set grid padding in physical pixels (all sides).
    void setGridPadding(float padding);

    /// Set minimum contrast ratio (WCAG 2.0). 1.0 = disabled, up to 21.0.
    void setMinimumContrast(float ratio);

    /// Set the URL highlight range for Cmd+hover underline rendering.
    /// row=-1 means no highlight.
    void setUrlHighlight(int row, int startCol, int endCol);

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

    /// Tab information for the tab bar.
    struct TabInfo {
        std::string title;
        std::string icon_name;         // OSC 1 icon name
        std::string process_name;      // foreground process (for icon selection)
        bool active = false;
        bool has_unread = false;       // show dot indicator
        bool needs_attention = false;  // highlight tab background
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
        // Process name -> icon codepoint hex string (from config)
        const std::unordered_map<std::string, std::string>* process_icon_map = nullptr;
    };

    /// Set tab bar content for rendering.
    void setTabBar(const TabBarInfo& info);
    /// Get current tab bar state (for hover updates).
    TabBarInfo getTabBar() const;

    /// Mark content as dirty so next render does a full rebuild.
    void markContentDirty() override;

    /// Set ghost text (dim suggestion) to display at the given row/col.
    /// Pass an empty string or row=-1 to clear.
    void setGhostText(const std::string& text, int row, int col);

    /// Return the atlas uploader for GPU texture management.
    IAtlasUploader* atlasUploader() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore

#endif // TERMCORE_METAL_TEXT_RENDERER_H
