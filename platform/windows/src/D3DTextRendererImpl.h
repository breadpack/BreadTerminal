#ifndef D3D_TEXT_RENDERER_IMPL_H
#define D3D_TEXT_RENDERER_IMPL_H

#if defined(_WIN32)

#include "D3DTextRenderer.h"
#include "D3DAtlasUploader.h"
#include "D3DImageRenderer.h"
#include "RenderSnapshot.h"
#include "ScreenSnapshot.h"
#include "termcore/dynamic_colors.h"
#include "termcore/font/ligature.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

namespace termcore {

// HLSL shader source for two-pass cell rendering (background + glyph + cursor).
extern const char* kCellShaderSource;

struct D3DTextRenderer::Impl {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;

    // Selection state
    D3DTextRenderer::Selection selection;

    // Search highlight state
    std::vector<D3DTextRenderer::SearchHighlight> searchHighlights;
    int searchCurrentIndex = -1;
    // Row-indexed search highlights for O(1) row lookup
    std::unordered_map<int, std::vector<std::pair<int,int>>> searchByRow; // row -> [(index, index)]
    void rebuildSearchIndex();

    // URL highlight state
    std::vector<D3DTextRenderer::UrlHighlight> urlHighlights;
    // Row-indexed URL highlights for O(1) row lookup
    std::unordered_map<int, std::vector<size_t>> urlByRow; // row -> [index into urlHighlights]
    void rebuildUrlIndex();

    // Background opacity (0.0-1.0), affects only bg cells and clear color
    float backgroundOpacity = 1.0f;

    // Cursor blink state
    bool cursorBlinkVisible = true;
    bool lastBlinkState = true;
    bool contentDirty = true;  // forces full rebuild on first frame
    bool imeActive = false;    // hide cursor during IME composition

    // Index in cellInstances where cursor instances begin
    // (everything from this index onward is cursor + overlays)
    size_t cellCountBeforeCursor = 0;

    // Status bar state
    D3DTextRenderer::StatusBarInfo statusBar;

    // Tab bar state
    D3DTextRenderer::TabBarInfo tabBar;

    // Pane border state
    D3DTextRenderer::PaneBorderInfo paneBorders;

    // Per-pane progress bars
    std::unordered_map<PaneId, D3DTextRenderer::PaneProgressInfo> paneProgress;

    // Per-pane status pills
    std::unordered_map<PaneId, std::vector<D3DTextRenderer::StatusPillInfo>> paneStatusPills;

    // Command palette state
    D3DTextRenderer::CommandPaletteInfo commandPalette;

    // Profile dropdown state
    D3DTextRenderer::ProfileDropdownInfo profileDropdown;

    // Sidebar state
    D3DTextRenderer::SidebarRenderInfo sidebar;

    // Resize overlay state
    bool resizeOverlayVisible = false;
    int resizeOverlayCols = 0;
    int resizeOverlayRows = 0;

    // Ghost text (dim suggestion text at cursor position)
    struct GhostText {
        std::string text;  // UTF-8 ghost text to display
        int row = -1;      // row to display at
        int col = -1;      // starting column
    };
    GhostText ghostText;

    // IME composition overlay (virtual, does not mutate Screen)
    ImeOverlay imeOverlay;

    // Font ligatures
    bool fontLigatures = true;
    LigatureDetector ligatureDetector;

    // Shaders
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;

    // Constant buffer
    ID3D11Buffer* constantBuffer = nullptr;

    // Structured buffer for cell instances
    ID3D11Buffer* cellBuffer = nullptr;
    ID3D11ShaderResourceView* cellBufferSRV = nullptr;
    UINT cellBufferCapacity = 0;

    // Sampler
    ID3D11SamplerState* sampler = nullptr;

    // Blend state
    ID3D11BlendState* blendState = nullptr;

    // Font stack (not owned)
    FontCollection* fontCollection = nullptr;
    GlyphCache* glyphCache = nullptr;
    GlyphAtlas* glyphAtlas = nullptr;
    IFontRasterizer* rasterizer = nullptr;

    // Atlas uploader
    std::unique_ptr<D3DAtlasUploader> atlasUploader;

    // Kitty graphics image renderer
    D3DImageRenderer imageRenderer;

    // Cached kitty graphics data for split-phase rendering
    // (captured during prepareFrame, used during submitFrame)
    const KittyGraphicsManager* cachedKittyGfx = nullptr;
    int64_t cachedViewportTopAbsRow = 0;
    int cachedVisibleRows = 0;
    float cachedFallbackBg[4] = {0, 0, 0, 1};

    // Viewport
    float viewportWidth = 0;
    float viewportHeight = 0;

    // Reusable buffer
    std::vector<D3DCellInstance> cellInstances;

    // Per-row caches for dirty row optimization.
    // When a row is clean (not dirty), we reuse cached instances instead of
    // re-shaping with HarfBuzz. Invalidated when grid dimensions change.
    struct RowCache {
        std::vector<D3DCellInstance> bgInstances;   // Pass 1 (background)
        std::vector<D3DCellInstance> fgInstances;   // Pass 2 (glyph/foreground)
        bool valid = false;
    };
    std::vector<RowCache> rowCaches;
    int cachedRows = 0;
    int cachedCols = 0;

    struct CellConstants {
        float viewport_size[2];
        float cell_size[2];
        float atlas_size[2];
        float _padding[2];
    };

    bool buildShaders();
    bool createResources();
    void ensureCellBuffer(UINT requiredCount);
    void cleanup();

    // Cell buffer construction (implemented in D3DCellBuilder.cpp)
    // Templated on ScreenT to support both Screen and ScreenSnapshot.
    static void colorFromRGBA(uint32_t rgba, float out[4]);
    bool isCellSelected(int row, int col) const;
    int searchHighlightType(int row, int col) const;
    const D3DTextRenderer::UrlHighlight* urlHighlightAt(int row, int col) const;

    template<typename ScreenT>
    void buildCellBuffer(const ScreenT& screen);

    template<typename ScreenT>
    void appendCursorInstances(const ScreenT& screen, float cellW, float cellH,
                               float gridOffsetX, float gridOffsetY);

    template<typename ScreenT>
    void patchCursorOnly(const ScreenT& screen);

    // Overlay passes (implemented in D3DCellBuilderOverlays.cpp)
    template<typename ScreenT>
    void buildOverlayPasses(const ScreenT& screen, float cellW, float cellH,
                            float ascent, float fontSize);

    // Pane status overlays (implemented in D3DCellBuilderPaneStatus.cpp)
    void buildPaneStatusOverlays(float cellW, float cellH,
                                 float ascent, float fontSize);

    // Command palette overlay (implemented in D3DCellBuilderCommandPalette.cpp)
    void buildCommandPaletteOverlay(float cellW, float cellH,
                                    float ascent, float fontSize);

    // Profile dropdown overlay (implemented in D3DCellBuilderProfileDropdown.cpp)
    void buildProfileDropdownOverlay(float cellW, float cellH,
                                     float ascent, float fontSize);

    // Sidebar overlay (implemented in D3DCellBuilderSidebar.cpp)
    void buildSidebarOverlay(float cellW, float cellH,
                             float ascent, float fontSize);
    float renderSidebarText(const std::string& text,
                            float startX, float baseY, float ascent,
                            float cellW, float fontSize,
                            uint32_t color, float maxX);
};

// Decode UTF-8 codepoint at position `pos`, advance `pos` past it.
inline char32_t nextCodepoint(const std::string& s, size_t& pos) {
    if (pos >= s.size()) return 0;
    auto b = static_cast<unsigned char>(s[pos]);
    if (b < 0x80) { ++pos; return b; }
    char32_t cp = 0;
    int extra = 0;
    if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; }
    else { ++pos; return 0; }
    if (pos + extra >= s.size()) { ++pos; return 0; }
    for (int i = 1; i <= extra; ++i) {
        auto c = static_cast<unsigned char>(s[pos + i]);
        if ((c & 0xC0) != 0x80) { ++pos; return 0; }
        cp = (cp << 6) | (c & 0x3F);
    }
    pos += extra + 1;
    return cp;
}

} // namespace termcore

#endif // _WIN32
#endif // D3D_TEXT_RENDERER_IMPL_H
