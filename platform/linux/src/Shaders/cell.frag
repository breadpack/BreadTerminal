#version 330 core

in vec2 v_texCoord;
in vec2 v_localCoord;
flat in vec4 v_fg_color;
flat in vec4 v_bg_color;
flat in uint v_flags;
flat in uint v_extra_flags;

uniform sampler2D u_atlas_r8;    // Grayscale text atlas
uniform sampler2D u_atlas_bgra;  // Color emoji atlas

out vec4 fragColor;

void main() {
    bool is_bg        = (v_flags & 4u) != 0u;
    bool has_glyph    = (v_flags & 1u) != 0u;
    bool is_color     = (v_flags & 2u) != 0u;
    bool is_cursor    = (v_flags & 8u) != 0u;
    bool is_underline = (v_flags & 16u) != 0u;

    // Background pass: solid background color
    if (is_bg) {
        fragColor = v_bg_color;
        return;
    }

    // Cursor pass: solid cursor color
    if (is_cursor) {
        fragColor = v_bg_color;
        return;
    }

    // Underline pass: procedural patterns
    if (is_underline) {
        uint ul_style = v_extra_flags & 7u;
        float local_x = v_localCoord.x;  // 0..1 across underline width
        float local_y = v_localCoord.y;  // 0..1 across underline height

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

    // Glyph pass: sample atlas texture
    vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
    if (has_glyph) {
        if (is_color) {
            // Color emoji: sample BGRA atlas, blend over background
            color = texture(u_atlas_bgra, v_texCoord);
        } else {
            // Grayscale text: alpha from R8 atlas, tinted with fg_color
            float alpha = texture(u_atlas_r8, v_texCoord).r;
            color = vec4(v_fg_color.rgb, alpha);
        }
    }

    fragColor = color;
}
