#include "GLTextRendererImpl.h"

namespace termcore {

// --- Shader sources ---

const char* kGLVertexShaderSource = R"(
#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_atlas_uv;
layout(location = 2) in vec2 a_atlas_size;
layout(location = 3) in vec2 a_glyph_offset;
layout(location = 4) in vec4 a_fg_color;
layout(location = 5) in vec4 a_bg_color;
layout(location = 6) in uint a_flags;
layout(location = 7) in uint a_extra_flags;

uniform vec2 u_viewport_size;
uniform vec2 u_cell_size;
uniform vec2 u_atlas_size;

out vec2 v_texCoord;
out vec2 v_localCoord;
flat out vec4 v_fg_color;
flat out vec4 v_bg_color;
flat out uint v_flags;
flat out uint v_extra_flags;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 corner = corners[gl_VertexID];

    bool is_bg = (a_flags & 4u) != 0u;
    bool is_cursor = (a_flags & 8u) != 0u;
    bool is_underline = (a_flags & 16u) != 0u;

    vec2 quad_size;
    if (is_bg) {
        quad_size = u_cell_size;
    } else if (is_cursor || is_underline) {
        quad_size = a_atlas_size;
    } else {
        quad_size = a_atlas_size;
    }

    vec2 pixel_pos = a_position + corner * quad_size;

    vec2 ndc = (pixel_pos / u_viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    gl_Position = vec4(ndc, 0.0, 1.0);

    v_texCoord = (a_atlas_uv + corner * a_atlas_size) / u_atlas_size;
    v_localCoord = corner;
    v_fg_color = a_fg_color;
    v_bg_color = a_bg_color;
    v_flags = a_flags;
    v_extra_flags = a_extra_flags;
}
)";

const char* kGLFragmentShaderSource = R"(
#version 330 core

in vec2 v_texCoord;
in vec2 v_localCoord;
flat in vec4 v_fg_color;
flat in vec4 v_bg_color;
flat in uint v_flags;
flat in uint v_extra_flags;

uniform sampler2D u_atlas_r8;
uniform sampler2D u_atlas_bgra;

out vec4 fragColor;

void main() {
    bool is_bg        = (v_flags & 4u) != 0u;
    bool has_glyph    = (v_flags & 1u) != 0u;
    bool is_color     = (v_flags & 2u) != 0u;
    bool is_cursor    = (v_flags & 8u) != 0u;
    bool is_underline = (v_flags & 16u) != 0u;

    if (is_bg) {
        fragColor = v_bg_color;
        return;
    }

    if (is_cursor) {
        fragColor = v_bg_color;
        return;
    }

    if (is_underline) {
        uint ul_style = v_extra_flags & 7u;
        float local_x = v_localCoord.x;
        float local_y = v_localCoord.y;

        if (ul_style == 3u) { // curly - sine wave
            float wave = sin(local_x * 3.14159 * 2.0) * 0.35 + 0.5;
            float dist = abs(local_y - wave);
            float alpha = 1.0 - smoothstep(0.0, 0.3, dist);
            fragColor = vec4(v_bg_color.rgb, alpha);
        } else if (ul_style == 4u) { // dotted
            float pattern = step(0.5, fract(local_x * 4.0));
            fragColor = vec4(v_bg_color.rgb, pattern);
        } else if (ul_style == 5u) { // dashed
            float pattern = step(0.33, fract(local_x * 2.0));
            fragColor = vec4(v_bg_color.rgb, pattern);
        } else {
            fragColor = v_bg_color; // single, double = solid
        }
        return;
    }

    vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
    if (has_glyph) {
        if (is_color) {
            color = texture(u_atlas_bgra, v_texCoord);
        } else {
            float alpha = texture(u_atlas_r8, v_texCoord).r;
            color = vec4(v_fg_color.rgb, alpha);
        }
    }
    fragColor = color;
}
)";

// --- Shader compilation helpers ---

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// --- Impl methods ---

bool GLTextRenderer::Impl::buildShaders() {
    GLuint vert = compileShader(GL_VERTEX_SHADER, kGLVertexShaderSource);
    if (!vert) return false;

    GLuint frag = compileShader(GL_FRAGMENT_SHADER, kGLFragmentShaderSource);
    if (!frag) {
        glDeleteShader(vert);
        return false;
    }

    program = linkProgram(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);

    if (!program) return false;

    uViewportSize = glGetUniformLocation(program, "u_viewport_size");
    uCellSize = glGetUniformLocation(program, "u_cell_size");
    uAtlasSize = glGetUniformLocation(program, "u_atlas_size");
    uAtlasR8 = glGetUniformLocation(program, "u_atlas_r8");
    uAtlasBGRA = glGetUniformLocation(program, "u_atlas_bgra");

    return true;
}

void GLTextRenderer::Impl::setupVAO() {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &instanceVBO);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

    size_t stride = sizeof(GLCellInstance);
    size_t offset = 0;

    // location 0: position (vec2)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(0, 1);
    offset += sizeof(float) * 2;

    // location 1: atlas_uv (vec2)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(1, 1);
    offset += sizeof(float) * 2;

    // location 2: atlas_size (vec2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(2, 1);
    offset += sizeof(float) * 2;

    // location 3: glyph_offset (vec2)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(3, 1);
    offset += sizeof(float) * 2;

    // location 4: fg_color (vec4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(4, 1);
    offset += sizeof(float) * 4;

    // location 5: bg_color (vec4)
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(5, 1);
    offset += sizeof(float) * 4;

    // location 6: flags (uint)
    glEnableVertexAttribArray(6);
    glVertexAttribIPointer(6, 1, GL_UNSIGNED_INT, stride,
                           reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(6, 1);
    offset += sizeof(uint32_t);

    // location 7: extra_flags (uint)
    glEnableVertexAttribArray(7);
    glVertexAttribIPointer(7, 1, GL_UNSIGNED_INT, stride,
                           reinterpret_cast<void*>(offset));
    glVertexAttribDivisor(7, 1);

    glBindVertexArray(0);
}

void GLTextRenderer::Impl::cleanup() {
    if (program) { glDeleteProgram(program); program = 0; }
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (instanceVBO) { glDeleteBuffers(1, &instanceVBO); instanceVBO = 0; }
}

// --- Public API ---

GLTextRenderer::GLTextRenderer()
    : impl_(std::make_unique<Impl>()) {}

GLTextRenderer::~GLTextRenderer() {
    impl_->cleanup();
}

bool GLTextRenderer::initialize() {
    impl_->atlasUploader = std::make_unique<GLAtlasUploader>();

    if (!impl_->buildShaders()) return false;
    impl_->setupVAO();
    return true;
}

void GLTextRenderer::setFontStack(FontCollection* collection,
                                   GlyphCache* cache,
                                   GlyphAtlas* atlas,
                                   IFontRasterizer* rasterizer) {
    impl_->fontCollection = collection;
    impl_->glyphCache = cache;
    impl_->glyphAtlas = atlas;
    impl_->rasterizer = rasterizer;
}

void GLTextRenderer::render(const Screen& screen) {
    if (!impl_->program) return;

    // Determine if only cursor blink changed (no content rebuild needed)
    bool blinkChanged = (impl_->cursorBlinkVisible != impl_->lastBlinkState);

    if (!impl_->contentDirty && blinkChanged
        && impl_->cellCountBeforeCursor > 0
        && !impl_->cellInstances.empty()) {
        impl_->patchCursorOnly(screen);
    } else {
        impl_->buildCellBuffer(screen);
        impl_->contentDirty = false;
    }
    impl_->lastBlinkState = impl_->cursorBlinkVisible;

    if (impl_->cellInstances.empty()) return;

    // Upload dirty atlas textures
    if (impl_->glyphAtlas) {
        impl_->atlasUploader->upload(*impl_->glyphAtlas);
    }

    // Clear with terminal background color
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    {
        uint32_t bg = screen.dynamicColors().background;
        clearColor[0] = static_cast<float>((bg >> 16) & 0xFF) / 255.0f;
        clearColor[1] = static_cast<float>((bg >> 8) & 0xFF) / 255.0f;
        clearColor[2] = static_cast<float>(bg & 0xFF) / 255.0f;
    }
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(impl_->program);

    // Set uniforms
    glUniform2f(impl_->uViewportSize,
                impl_->viewportWidth, impl_->viewportHeight);

    if (impl_->fontCollection) {
        FontMetrics m = impl_->fontCollection->primaryMetrics();
        glUniform2f(impl_->uCellSize, m.cell_width, m.cell_height);
    }

    if (impl_->glyphAtlas) {
        const AtlasPage* r8Page = impl_->glyphAtlas->getPage(AtlasFormat::R8);
        if (r8Page) {
            glUniform2f(impl_->uAtlasSize,
                        static_cast<float>(r8Page->width()),
                        static_cast<float>(r8Page->height()));
        }
    }

    // Bind atlas textures
    GLuint r8Tex = impl_->atlasUploader->textureForFormat(AtlasFormat::R8);
    GLuint bgraTex = impl_->atlasUploader->textureForFormat(AtlasFormat::BGRA);

    glActiveTexture(GL_TEXTURE0);
    if (r8Tex) glBindTexture(GL_TEXTURE_2D, r8Tex);
    glUniform1i(impl_->uAtlasR8, 0);

    glActiveTexture(GL_TEXTURE1);
    if (bgraTex) glBindTexture(GL_TEXTURE_2D, bgraTex);
    glUniform1i(impl_->uAtlasBGRA, 1);

    // Upload instance data
    glBindVertexArray(impl_->vao);
    glBindBuffer(GL_ARRAY_BUFFER, impl_->instanceVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 impl_->cellInstances.size() * sizeof(GLCellInstance),
                 impl_->cellInstances.data(), GL_STREAM_DRAW);

    // Instanced draw: 6 vertices per quad, N instances
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6,
                          static_cast<GLsizei>(impl_->cellInstances.size()));

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

void GLTextRenderer::resize(float width, float height) {
    impl_->viewportWidth = width;
    impl_->viewportHeight = height;
    impl_->contentDirty = true;
    glViewport(0, 0, static_cast<GLsizei>(width),
               static_cast<GLsizei>(height));
}

void GLTextRenderer::setSelection(const Selection& sel) {
    impl_->selection = sel;
    impl_->contentDirty = true;
}

void GLTextRenderer::setCursorBlink(bool visible) {
    impl_->cursorBlinkVisible = visible;
}

void GLTextRenderer::markContentDirty() {
    impl_->contentDirty = true;
}

IAtlasUploader* GLTextRenderer::atlasUploader() {
    return impl_->atlasUploader.get();
}

void GLTextRenderer::setSearchHighlights(
        const std::vector<SearchHighlight>& highlights, int currentIndex) {
    impl_->searchHighlights = highlights;
    impl_->searchCurrentIndex = currentIndex;
    impl_->rebuildSearchIndex();
    impl_->contentDirty = true;
}

void GLTextRenderer::setStatusBar(const StatusBarInfo& info) {
    impl_->statusBar = info;
    impl_->contentDirty = true;
}

void GLTextRenderer::setResizeOverlay(bool visible, int cols, int rows) {
    impl_->resizeOverlayVisible = visible;
    impl_->resizeOverlayCols = cols;
    impl_->resizeOverlayRows = rows;
    impl_->contentDirty = true;
}

void GLTextRenderer::setTabBar(const TabBarInfo& info) {
    impl_->tabBar = info;
    impl_->contentDirty = true;
}

GLTextRenderer::TabBarInfo GLTextRenderer::getTabBar() const {
    return impl_->tabBar;
}

void GLTextRenderer::setPaneBorders(const PaneBorderInfo& info) {
    impl_->paneBorders = info;
    impl_->contentDirty = true;
}

void GLTextRenderer::setPaneProgress(PaneId pane_id, const PaneProgressInfo& info) {
    if (info.progress < 0.0f) {
        impl_->paneProgress.erase(pane_id);
    } else {
        impl_->paneProgress[pane_id] = info;
    }
    impl_->contentDirty = true;
}

void GLTextRenderer::setPaneStatusPills(PaneId pane_id,
                                         const std::vector<StatusPillInfo>& pills) {
    if (pills.empty()) {
        impl_->paneStatusPills.erase(pane_id);
    } else {
        impl_->paneStatusPills[pane_id] = pills;
    }
    impl_->contentDirty = true;
}

void GLTextRenderer::setCommandPalette(const CommandPaletteInfo& info) {
    impl_->commandPalette = info;
}

void GLTextRenderer::setBackgroundOpacity(float opacity) {
    impl_->backgroundOpacity = opacity;
    impl_->contentDirty = true;
}

void GLTextRenderer::setGhostText(const std::string& text, int row, int col) {
    impl_->ghostText.text = text;
    impl_->ghostText.row = row;
    impl_->ghostText.col = col;
    impl_->contentDirty = true;
}

void GLTextRenderer::setFontLigatures(bool enabled) {
    impl_->fontLigatures = enabled;
    impl_->contentDirty = true;
}

} // namespace termcore
