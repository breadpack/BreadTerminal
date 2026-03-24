// BreadTerminal - D3D11 cell rendering shader
// Vertex shader maps pixel coords to NDC.
// Pixel shader samples atlas texture and blends with cell colors.
// NOTE: This file is a reference copy. The actual shader source is
//       embedded in D3DTextRenderer.cpp as kCellShaderSource.

cbuffer CellConstants : register(b0) {
    float2 viewport_size;   // Window dimensions in pixels
    float2 cell_size;       // Single cell dimensions in pixels
    float2 atlas_size;      // Atlas texture dimensions in pixels
    float2 _padding;        // Align to 16 bytes
};

// Atlas textures
Texture2D atlas_r8   : register(t0);   // Grayscale text atlas
Texture2D atlas_bgra : register(t1);   // Color emoji atlas
SamplerState atlas_sampler : register(s0);

// Per-cell instance data (StructuredBuffer)
struct CellInstance {
    float2 position;        // Screen position (top-left of cell, pixels)
    float2 atlas_uv;        // Top-left UV in atlas (pixels)
    float2 atlas_size_px;   // Glyph size in atlas (pixels)
    float2 glyph_offset;    // Bearing offset within cell
    float4 fg_color;        // Foreground RGBA (0-1)
    float4 bg_color;        // Background RGBA (0-1)
    uint   flags;           // bit0=has_glyph, bit1=is_color, bit2=is_bg_pass, bit3=is_cursor, bit4=is_underline, bit5=is_rounded_rect_top
    uint   extra_flags;     // bits 0-2: underline_style; bits 16-31: corner_radius * 16 (fixed-point)
};

StructuredBuffer<CellInstance> cells : register(t2);

struct VS_OUTPUT {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 fg_color : COLOR0;
    float4 bg_color : COLOR1;
    nointerpolation float2 quad_size_px : TEXCOORD1;
    nointerpolation float  corner_radius : TEXCOORD2;
    uint   flags       : BLENDINDICES0;
    uint   extra_flags : BLENDINDICES1;
};

// Vertex shader: expand each cell instance into a 2-triangle quad
VS_OUTPUT VSMain(uint vertex_id : SV_VertexID,
                 uint instance_id : SV_InstanceID) {
    VS_OUTPUT output;

    // 6 vertices for 2 triangles forming a quad
    float2 corners[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
    };
    float2 corner = corners[vertex_id];

    CellInstance cell = cells[instance_id];

    bool is_bg = (cell.flags & 4u) != 0u;
    bool is_rounded = (cell.flags & 32u) != 0u;

    float2 quad_size = is_bg ? cell_size : cell.atlas_size_px;
    float2 pixel_pos = cell.position + corner * quad_size;

    float2 ndc = (pixel_pos / viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y: D3D NDC top is +1, we want top-down

    output.position = float4(ndc, 0.0, 1.0);

    // Decode corner radius from upper 16 bits of extra_flags (fixed-point * 16)
    float radius = float(cell.extra_flags >> 16u) / 16.0;

    if (is_rounded) {
        // For rounded rects, pass normalized quad UV and pixel size
        output.texCoord = corner;
        output.quad_size_px = quad_size;
        output.corner_radius = radius;
    } else {
        // Atlas texture coordinate (pixel coords -> normalized)
        output.texCoord = (cell.atlas_uv + corner * cell.atlas_size_px)
                          / atlas_size;
        output.quad_size_px = float2(0.0, 0.0);
        output.corner_radius = 0.0;
    }

    bool is_underline = (cell.flags & 16u) != 0u;
    output.fg_color = is_underline ? float4(corner.x, corner.y, 0.0, 0.0) : cell.fg_color;
    output.bg_color = cell.bg_color;
    output.flags = cell.flags;
    output.extra_flags = cell.extra_flags;

    return output;
}

// Pixel shader: render cell background, then blend glyph on top
float4 PSMain(VS_OUTPUT input) : SV_Target {
    bool is_bg     = (input.flags & 4u) != 0u;
    bool has_glyph = (input.flags & 1u) != 0u;
    bool is_color  = (input.flags & 2u) != 0u;
    bool is_cursor = (input.flags & 8u) != 0u;
    bool is_rounded = (input.flags & 32u) != 0u;

    if (is_rounded) {
        // SDF-based rounded rectangle with top corners only
        float2 size = input.quad_size_px;
        float radius = input.corner_radius;
        // UV is 0..1 across the quad
        float2 px = input.texCoord * size;

        // Distance from each edge
        float2 halfSize = size * 0.5;
        float2 p = abs(px - halfSize);

        // Only round top corners: apply radius when in top half, 0 for bottom
        float r = (input.texCoord.y < 0.5) ? radius : 0.0;
        float2 q = p - halfSize + float2(r, r);
        float d = length(max(q, float2(0.0, 0.0))) - r;

        // Anti-aliased edge (1px smooth transition)
        float alpha = 1.0 - smoothstep(-0.5, 0.5, d);

        // Apply alpha to premultiplied color
        float4 col = input.bg_color;
        return float4(col.rgb * alpha, col.a * alpha);
    }

    if (is_bg) {
        return input.bg_color;
    }

    if (is_cursor) {
        return input.bg_color;
    }

    bool is_underline = (input.flags & 16u) != 0u;
    if (is_underline) {
        uint ul_style = input.extra_flags & 7u;
        float local_x = input.fg_color.x;  // 0..1 across underline width
        float local_y = input.fg_color.y;  // 0..1 across underline height

        if (ul_style == 3u) { // curly - sine wave
            float wave = sin(local_x * 3.14159 * 2.0) * 0.35 + 0.5;
            float dist = abs(local_y - wave);
            float a = 1.0 - smoothstep(0.0, 0.3, dist);
            return float4(input.bg_color.rgb * a, a);
        }
        if (ul_style == 4u) { // dotted
            float a = step(0.5, frac(local_x * 4.0));
            return float4(input.bg_color.rgb * a, a);
        }
        if (ul_style == 5u) { // dashed
            float a = step(0.33, frac(local_x * 2.0));
            return float4(input.bg_color.rgb * a, a);
        }
        return input.bg_color; // single, double = solid (already premultiplied)
    }

    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    if (has_glyph) {
        if (is_color) {
            // Color emoji: already in BGRA, premultiply
            float4 tex = atlas_bgra.Sample(atlas_sampler, input.texCoord);
            color = float4(tex.rgb * tex.a, tex.a);
        } else {
            // Mono glyph: alpha from R8 atlas, premultiply fg color
            float alpha = atlas_r8.Sample(atlas_sampler, input.texCoord).r;
            color = float4(input.fg_color.rgb * alpha, alpha);
        }
    }
    return color;
}
