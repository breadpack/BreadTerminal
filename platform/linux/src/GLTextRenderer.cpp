#include "GLTextRenderer.h"
#include "GLAtlasUploader.h"

#include <epoxy/gl.h>
#include <cstring>
#include <vector>
#include <algorithm>

namespace termcore {

namespace {

// Inline shader sources (matching the .vert/.frag files)
const char* kVertexShaderSource = R"(
#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_atlas_uv;
layout(location = 2) in vec2 a_atlas_size;
layout(location = 3) in vec2 a_glyph_offset;
layout(location = 4) in vec4 a_fg_color;
layout(location = 5) in vec4 a_bg_color;
layout(location = 6) in uint a_flags;

uniform vec2 u_viewport_size;
uniform vec2 u_cell_size;
uniform vec2 u_atlas_size;

out vec2 v_texCoord;
flat out vec4 v_fg_color;
flat out vec4 v_bg_color;
flat out uint v_flags;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 corner = corners[gl_VertexID];
    vec2 pixel_pos = a_position + corner * u_cell_size;
    vec2 ndc = (pixel_pos / u_viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_texCoord = (a_atlas_uv + corner * a_atlas_size) / u_atlas_size;
    v_fg_color = a_fg_color;
    v_bg_color = a_bg_color;
    v_flags = a_flags;
}
)";

const char* kFragmentShaderSource = R"(
#version 330 core

in vec2 v_texCoord;
flat in vec4 v_fg_color;
flat in vec4 v_bg_color;
flat in uint v_flags;

uniform sampler2D u_atlas_r8;
uniform sampler2D u_atlas_bgra;

out vec4 fragColor;

void main() {
    vec4 color = v_bg_color;
    bool has_glyph = (v_flags & 1u) != 0u;
    bool is_color  = (v_flags & 2u) != 0u;
    if (has_glyph) {
        if (is_color) {
            vec4 glyph_color = texture(u_atlas_bgra, v_texCoord);
            color = mix(color, glyph_color, glyph_color.a);
        } else {
            float alpha = texture(u_atlas_r8, v_texCoord).r;
            color = mix(color, v_fg_color, alpha);
        }
    }
    fragColor = color;
}
)";

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        // In production, log the error
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

} // namespace

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

    // Font stack (not owned)
    FontCollection* fontCollection = nullptr;
    GlyphCache* glyphCache = nullptr;
    GlyphAtlas* glyphAtlas = nullptr;
    IFontRasterizer* rasterizer = nullptr;

    // Atlas uploader
    std::unique_ptr<GLAtlasUploader> atlasUploader;

    // Viewport
    float viewportWidth = 0;
    float viewportHeight = 0;

    // Reusable buffer
    std::vector<GLCellInstance> cellInstances;

    bool buildShaders() {
        GLuint vert = compileShader(GL_VERTEX_SHADER, kVertexShaderSource);
        if (!vert) return false;

        GLuint frag = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
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

    void setupVAO() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &instanceVBO);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);

        // Layout matches GLCellInstance struct
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

        glBindVertexArray(0);
    }

    static void colorFromRGBA(uint32_t rgba, float out[4]) {
        out[0] = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
        out[1] = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
        out[2] = static_cast<float>(rgba & 0xFF) / 255.0f;
        out[3] = 1.0f;
    }

    void buildCellBuffer(const Screen& screen) {
        if (!fontCollection || !glyphCache || !glyphAtlas || !rasterizer) {
            return;
        }

        FontMetrics metrics = fontCollection->primaryMetrics();
        float cellW = metrics.cell_width;
        float cellH = metrics.cell_height;
        float fontSize = fontCollection->fontSize();

        int rows = screen.rows();
        int cols = screen.cols();

        cellInstances.clear();
        cellInstances.reserve(rows * cols);

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const TermCell& cell = screen.cellAt(row, col);

                GLCellInstance inst = {};
                inst.position[0] = col * cellW;
                inst.position[1] = row * cellH;

                colorFromRGBA(cell.fg_color, inst.fg_color);
                colorFromRGBA(cell.bg_color, inst.bg_color);

                if (cell.attributes & AttrInverse) {
                    std::swap(inst.fg_color[0], inst.bg_color[0]);
                    std::swap(inst.fg_color[1], inst.bg_color[1]);
                    std::swap(inst.fg_color[2], inst.bg_color[2]);
                    std::swap(inst.fg_color[3], inst.bg_color[3]);
                }

                inst.flags = 0;

                if (cell.codepoint != ' ' && cell.codepoint != 0) {
                    CollectionFaceId faceId =
                        fontCollection->resolveFace(cell.codepoint);
                    if (faceId != kInvalidCollectionFace) {
                        FontFaceId rastFace =
                            fontCollection->rasterizerFaceId(faceId);
                        uint32_t glyphIdx =
                            rasterizer->getGlyphIndex(rastFace,
                                                      cell.codepoint);
                        if (glyphIdx != 0) {
                            GlyphKey key{rastFace, glyphIdx, {0, 0}};
                            auto info = glyphCache->getOrRasterize(
                                key, fontSize, *rasterizer, *glyphAtlas);
                            if (info) {
                                inst.flags |= 1;
                                if (info->is_color) inst.flags |= 2;
                                inst.atlas_uv[0] =
                                    static_cast<float>(info->region.x);
                                inst.atlas_uv[1] =
                                    static_cast<float>(info->region.y);
                                inst.atlas_size[0] =
                                    static_cast<float>(info->region.width);
                                inst.atlas_size[1] =
                                    static_cast<float>(info->region.height);
                                inst.glyph_offset[0] =
                                    static_cast<float>(info->region.bearing_x);
                                inst.glyph_offset[1] =
                                    static_cast<float>(info->region.bearing_y);
                            }
                        }
                    }
                }

                cellInstances.push_back(inst);
            }
        }
    }

    void cleanup() {
        if (program) { glDeleteProgram(program); program = 0; }
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (instanceVBO) { glDeleteBuffers(1, &instanceVBO); instanceVBO = 0; }
    }
};

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

    impl_->buildCellBuffer(screen);
    if (impl_->cellInstances.empty()) return;

    // Upload dirty atlas textures
    if (impl_->glyphAtlas) {
        impl_->atlasUploader->upload(*impl_->glyphAtlas);
    }

    // Clear
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
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
    glViewport(0, 0, static_cast<GLsizei>(width),
               static_cast<GLsizei>(height));
}

} // namespace termcore
