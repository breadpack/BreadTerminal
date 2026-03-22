#version 330 core

// Per-instance cell data
layout(location = 0) in vec2 a_position;      // Screen position (top-left, pixels)
layout(location = 1) in vec2 a_atlas_uv;      // Top-left UV in atlas (pixels)
layout(location = 2) in vec2 a_atlas_size;     // Glyph size in atlas (pixels)
layout(location = 3) in vec2 a_glyph_offset;   // Bearing offset within cell
layout(location = 4) in vec4 a_fg_color;       // Foreground color (RGBA, 0-1)
layout(location = 5) in vec4 a_bg_color;       // Background color (RGBA, 0-1)
layout(location = 6) in uint a_flags;          // Bit flags: bit0=has_glyph, bit1=is_color, bit2=is_bg, bit3=is_cursor, bit4=is_underline
layout(location = 7) in uint a_extra_flags;    // bits 0-2: underline_style

// Uniforms
uniform vec2 u_viewport_size;
uniform vec2 u_cell_size;
uniform vec2 u_atlas_size;

// Outputs to fragment shader
out vec2 v_texCoord;
out vec2 v_localCoord;
flat out vec4 v_fg_color;
flat out vec4 v_bg_color;
flat out uint v_flags;
flat out uint v_extra_flags;

void main() {
    // Each cell is a quad drawn as 2 triangles (6 vertices via gl_VertexID)
    vec2 corners[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    vec2 corner = corners[gl_VertexID];

    bool is_bg = (a_flags & 4u) != 0u;
    bool is_cursor = (a_flags & 8u) != 0u;
    bool is_underline = (a_flags & 16u) != 0u;

    // Determine quad size based on instance type
    vec2 quad_size;
    if (is_bg) {
        quad_size = u_cell_size;
    } else {
        quad_size = a_atlas_size;  // glyph-sized, cursor-sized, or underline-sized
    }

    // Screen position: pixel coords -> NDC
    vec2 pixel_pos = a_position + corner * quad_size;
    vec2 ndc = (pixel_pos / u_viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y (OpenGL NDC is bottom-up)

    gl_Position = vec4(ndc, 0.0, 1.0);

    // Texture coordinate (atlas UV in pixels -> normalized)
    v_texCoord = (a_atlas_uv + corner * a_atlas_size) / u_atlas_size;

    // Local coordinate for underline patterns
    v_localCoord = corner;

    v_fg_color = a_fg_color;
    v_bg_color = a_bg_color;
    v_flags = a_flags;
    v_extra_flags = a_extra_flags;
}
