#include <metal_stdlib>
using namespace metal;

// Per-instance cell data (passed as instance buffer)
struct CellInstance {
    float2 position;       // Screen position (top-left of cell, in pixels)
    float2 atlas_uv;       // Top-left UV in atlas (in pixels, not normalized)
    float2 atlas_size;     // Size of glyph in atlas (pixels)
    float2 glyph_offset;   // Bearing offset within cell
    float4 fg_color;       // Foreground color (RGBA, 0-1)
    float4 bg_color;       // Background color (RGBA, 0-1)
    uint flags;            // Bit flags: bit0=has_glyph, bit1=is_color
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 fg_color;
    float4 bg_color;
    uint flags;
};

// Uniforms
struct Uniforms {
    float2 viewport_size;  // Window size in pixels
    float2 cell_size;      // Cell dimensions in pixels
    float2 atlas_size;     // Atlas texture size in pixels
};

vertex VertexOut cell_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant CellInstance* cells [[buffer(0)]],
    constant Uniforms& uniforms [[buffer(1)]]
) {
    // Each cell is a quad (2 triangles, 6 vertices)
    float2 corners[] = {
        {0, 0}, {1, 0}, {0, 1},  // triangle 1
        {1, 0}, {1, 1}, {0, 1}   // triangle 2
    };

    CellInstance cell = cells[instance_id];
    float2 corner = corners[vertex_id];

    // Screen position (pixel coords -> NDC)
    float2 pixel_pos = cell.position + corner * uniforms.cell_size;
    float2 ndc = (pixel_pos / uniforms.viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y (Metal NDC is bottom-up)

    // Texture coordinate
    float2 tex_coord = (cell.atlas_uv + corner * cell.atlas_size) / uniforms.atlas_size;

    VertexOut out;
    out.position = float4(ndc, 0, 1);
    out.texCoord = tex_coord;
    out.fg_color = cell.fg_color;
    out.bg_color = cell.bg_color;
    out.flags = cell.flags;
    return out;
}

fragment float4 cell_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> atlas_r8 [[texture(0)]],
    texture2d<float> atlas_bgra [[texture(1)]]
) {
    constexpr sampler s(mag_filter::nearest, min_filter::nearest);

    // Draw background first
    float4 color = in.bg_color;

    bool has_glyph = (in.flags & 1) != 0;
    bool is_color = (in.flags & 2) != 0;

    if (has_glyph) {
        if (is_color) {
            // Color emoji: sample BGRA atlas, blend over background
            float4 glyph_color = atlas_bgra.sample(s, in.texCoord);
            color = mix(color, glyph_color, glyph_color.a);
        } else {
            // Grayscale text: alpha from R8 atlas, tinted with fg_color
            float alpha = atlas_r8.sample(s, in.texCoord).r;
            color = mix(color, in.fg_color, alpha);
        }
    }

    return color;
}
