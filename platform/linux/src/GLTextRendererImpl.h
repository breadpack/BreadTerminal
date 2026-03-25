#ifndef GL_TEXT_RENDERER_IMPL_H
#define GL_TEXT_RENDERER_IMPL_H

#include "GLTextRenderer.h"
#include "GLAtlasUploader.h"
#include "termcore/dynamic_colors.h"

#include <epoxy/gl.h>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace termcore {

// Shader compilation helpers
GLuint compileShader(GLenum type, const char* source);
GLuint linkProgram(GLuint vert, GLuint frag);

// Inline shader sources
extern const char* kGLVertexShaderSource;
extern const char* kGLFragmentShaderSource;

struct GLTextRenderer::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint instanceVBO = 0;

    // Uniform locations
    GLint uViewportSize = -1;
    GLint uCellSize = -1;
    GLint uAtlasSize = -1;
    GLint uAtlasR8 = -1;
    GLint uAtlasBGRA = -1;

    // Selection state
    GLTextRenderer::Selection selection;

    // Search highlight state
    std::vector<GLTextRenderer::SearchHighlight> searchHighlights;
    int searchCurrentIndex = -1;
    // Row-indexed search highlights for O(1) row lookup
    std::unordered_map<int, std::vector<std::pair<int,int>>> searchByRow; // row -> [(index, index)]
    void rebuildSearchIndex();

    // Cursor blink state
    bool cursorBlinkVisible = true;
    bool lastBlinkState = true;
    bool contentDirty = true;  // forces full rebuild on first frame

    // Index in cellInstances where cursor instances begin
    size_t cellCountBeforeCursor = 0;

    // Status bar state
    GLTextRenderer::StatusBarInfo statusBar;

    // Tab bar state
    GLTextRenderer::TabBarInfo tabBar;

    // Pane border state
    GLTextRenderer::PaneBorderInfo paneBorders;

    // Per-pane progress bars
    std::unordered_map<PaneId, GLTextRenderer::PaneProgressInfo> paneProgress;

    // Per-pane status pills
    std::unordered_map<PaneId, std::vector<GLTextRenderer::StatusPillInfo>> paneStatusPills;

    // Command palette state
    GLTextRenderer::CommandPaletteInfo commandPalette;

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

    // Font stack (not owned)
    FontCollection* fontCollection = nullptr;
    GlyphCache* glyphCache = nullptr;
    GlyphAtlas* glyphAtlas = nullptr;
    IFontRasterizer* rasterizer = nullptr;

    // Atlas uploader
    std::unique_ptr<GLAtlasUploader> atlasUploader;

    // Background opacity (1.0 = fully opaque, <1.0 for transparency)
    float backgroundOpacity = 1.0f;

    // Viewport
    float viewportWidth = 0;
    float viewportHeight = 0;

    // Reusable buffer
    std::vector<GLCellInstance> cellInstances;

    bool buildShaders();
    void setupVAO();
    void cleanup();

    // Cell buffer construction (implemented in GLCellBuilder.cpp)
    static void colorFromRGBA(uint32_t rgba, float out[4]);
    bool isCellSelected(int row, int col) const;
    int searchHighlightType(int row, int col) const;
    void buildCellBuffer(const Screen& screen);
    void appendCursorInstances(const Screen& screen, float cellW, float cellH,
                               float gridOffsetY);
    void patchCursorOnly(const Screen& screen);

    // Overlay passes (implemented in GLCellBuilderOverlays.cpp)
    void buildOverlayPasses(const Screen& screen, float cellW, float cellH,
                            float ascent, float fontSize);

    // Pane status overlays (implemented in GLCellBuilderPaneStatus.cpp)
    void buildPaneStatusOverlays(float cellW, float cellH,
                                 float ascent, float fontSize);

    // Command palette overlay (implemented in GLCellBuilderCommandPalette.cpp)
    void buildCommandPaletteOverlay(float cellW, float cellH,
                                    float ascent, float fontSize);
};

} // namespace termcore

#endif // GL_TEXT_RENDERER_IMPL_H
