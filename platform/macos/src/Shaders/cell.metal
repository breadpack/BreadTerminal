#include <metal_stdlib>
using namespace metal;

// Per-instance cell data -- Ghostty-style compact layout (32 bytes).
// Must match CellInstance in MetalTextRenderer.h exactly.
struct CellInstance {
    ushort2 grid_pos;         // col, row
    ushort2 glyph_uv;        // atlas x, y
    ushort2 glyph_size;      // atlas w, h
    short2  offset;           // bearing offset (signed)
    uchar4  fg_color;         // RGBA
    uchar4  bg_color;         // RGBA
    uchar   flags;
    uchar3  _pad;
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 fg_color;
    float4 bg_color;
    uint flags [[flat]];
};

struct Uniforms {
    float2 viewport_size;
    float2 cell_size;
    float2 atlas_size;
    float2 grid_padding;
};

vertex VertexOut cell_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant CellInstance* cells [[buffer(0)]],
    constant Uniforms& u [[buffer(1)]]
) {
    // 6 vertices = 2 triangles forming a quad
    float2 corners[] = {
        {0,0}, {1,0}, {0,1},
        {1,0}, {1,1}, {0,1}
    };

    CellInstance cell = cells[instance_id];
    float2 corner = corners[vertex_id];
    bool is_bg = (cell.flags & 4) != 0;

    // Cell origin in pixels
    float2 cell_origin = float2(cell.grid_pos) * u.cell_size + u.grid_padding;

    // Colors: convert uchar4 to float4
    float4 fg = float4(cell.fg_color) / 255.0;
    float4 bg = float4(cell.bg_color) / 255.0;

    float2 pixel_pos;
    float2 tex_coord;

    if (is_bg) {
        // Background: full cell quad
        pixel_pos = cell_origin + corner * u.cell_size;
        tex_coord = float2(0);
    } else {
        // Glyph: actual glyph-sized quad with bearing offset
        float2 glyph_size = float2(cell.glyph_size);
        float2 bearing = float2(cell.offset);
        // offset_y is pre-computed as: ascent - bearing_y (distance from cell top)
        float2 glyph_origin = cell_origin + bearing;
        pixel_pos = glyph_origin + corner * glyph_size;

        tex_coord = float2(cell.glyph_uv) + corner * glyph_size;
    }

    // Pixel coords -> NDC
    float2 ndc = (pixel_pos / u.viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y (Metal NDC is bottom-up)

    VertexOut out;
    out.position = float4(ndc, 0, 1);
    out.texCoord = tex_coord;
    out.fg_color = fg;
    out.bg_color = bg;
    out.flags = uint(cell.flags);
    return out;
}

fragment float4 cell_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> atlas_gray [[texture(0)]],
    texture2d<float> atlas_color [[texture(1)]]
) {
    if ((in.flags & 4) != 0) {
        // Background pass -- pre-multiply alpha for transparency compositing
        float a = in.bg_color.a;
        return float4(in.bg_color.rgb * a, a);
    }

    bool is_color = (in.flags & 2) != 0;

    if (is_color) {
        constexpr sampler emojiSampler(coord::pixel, address::clamp_to_edge, filter::linear);
        return atlas_color.sample(emojiSampler, in.texCoord);
    } else {
        constexpr sampler textSampler(coord::pixel, address::clamp_to_edge, filter::nearest);
        float alpha = atlas_gray.sample(textSampler, in.texCoord).r;
        return float4(in.fg_color.rgb * alpha, alpha);
    }
}
