// BreadTerminal - D3D11 cell rendering shader
// Vertex shader maps pixel coords to NDC.
// Pixel shader samples atlas texture and blends with cell colors.

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
    uint   flags;           // bit0=has_glyph, bit1=is_color
};

StructuredBuffer<CellInstance> cells : register(t2);

struct VS_OUTPUT {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 fg_color : COLOR0;
    float4 bg_color : COLOR1;
    uint   flags    : BLENDINDICES0;
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

    // Map pixel position to NDC [-1, 1]
    float2 pixel_pos = cell.position + corner * cell_size;
    float2 ndc = (pixel_pos / viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;  // Flip Y: D3D NDC top is +1, we want top-down

    output.position = float4(ndc, 0.0, 1.0);

    // Atlas texture coordinate (pixel coords -> normalized)
    output.texCoord = (cell.atlas_uv + corner * cell.atlas_size_px)
                      / atlas_size;

    output.fg_color = cell.fg_color;
    output.bg_color = cell.bg_color;
    output.flags = cell.flags;

    return output;
}

// Pixel shader: render cell background, then blend glyph on top
float4 PSMain(VS_OUTPUT input) : SV_Target {
    // Start with background
    float4 color = input.bg_color;

    bool has_glyph = (input.flags & 1u) != 0u;
    bool is_color  = (input.flags & 2u) != 0u;

    if (has_glyph) {
        if (is_color) {
            // Color emoji: sample BGRA atlas, alpha blend
            float4 glyph_color = atlas_bgra.Sample(
                atlas_sampler, input.texCoord);
            color = lerp(color, glyph_color, glyph_color.a);
        } else {
            // Monochrome text: alpha from R8 atlas, tint with fg color
            float alpha = atlas_r8.Sample(
                atlas_sampler, input.texCoord).r;
            color = lerp(color, input.fg_color, alpha);
        }
    }

    return color;
}
