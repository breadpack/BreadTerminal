#version 330 core

in vec2 v_texCoord;
flat in vec4 v_fg_color;
flat in vec4 v_bg_color;
flat in uint v_flags;

uniform sampler2D u_atlas_r8;    // Grayscale text atlas
uniform sampler2D u_atlas_bgra;  // Color emoji atlas

out vec4 fragColor;

void main() {
    // Start with background color
    vec4 color = v_bg_color;

    bool has_glyph = (v_flags & 1u) != 0u;
    bool is_color  = (v_flags & 2u) != 0u;

    if (has_glyph) {
        if (is_color) {
            // Color emoji: sample BGRA atlas, blend over background
            vec4 glyph_color = texture(u_atlas_bgra, v_texCoord);
            color = mix(color, glyph_color, glyph_color.a);
        } else {
            // Grayscale text: alpha from R8 atlas, tinted with fg_color
            float alpha = texture(u_atlas_r8, v_texCoord).r;
            color = mix(color, v_fg_color, alpha);
        }
    }

    fragColor = color;
}
