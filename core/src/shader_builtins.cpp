#include "termcore/shader_effect.h"

namespace termcore {

// ---------------------------------------------------------------------------
// HLSL post-processing shaders (Direct3D 11, ps_5_0 / vs_5_0)
// Each shader is a full-screen triangle pass that samples the scene texture.
// Uniforms are supplied via constant buffer b0.
// ---------------------------------------------------------------------------

static const char* kHLSL_CRT = R"(
cbuffer PostFX : register(b0) {
    float u_time;
    float u_resolution_x;
    float u_resolution_y;
    float u_intensity;
    float u_custom[8];
};

Texture2D scene : register(t0);
SamplerState samp : register(s0);

struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VS_OUT VSMain(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv.y = 1.0 - o.uv.y;
    return o;
}

float4 PSMain(VS_OUT input) : SV_Target {
    float2 uv = input.uv;
    float2 center = uv - 0.5;
    float r2 = dot(center, center);
    float barrel = 1.0 + r2 * 0.3 * u_intensity;
    float2 distorted = center * barrel + 0.5;

    if (distorted.x < 0.0 || distorted.x > 1.0 ||
        distorted.y < 0.0 || distorted.y > 1.0)
        return float4(0, 0, 0, 1);

    float3 col = scene.Sample(samp, distorted).rgb;
    float scanline = sin(distorted.y * u_resolution_y * 3.14159) * 0.5 + 0.5;
    col *= lerp(1.0, scanline, 0.15 * u_intensity);
    float phosphor = 1.0 + 0.03 * sin(distorted.x * u_resolution_x * 3.14159 * 3.0);
    col *= phosphor;
    float vignette = 1.0 - r2 * 1.5 * u_intensity;
    col *= saturate(vignette);
    return float4(col, 1.0);
}
)";

static const char* kHLSL_Bloom = R"(
cbuffer PostFX : register(b0) {
    float u_time;
    float u_resolution_x;
    float u_resolution_y;
    float u_intensity;
    float u_custom[8];
};

Texture2D scene : register(t0);
SamplerState samp : register(s0);

struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VS_OUT VSMain(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv.y = 1.0 - o.uv.y;
    return o;
}

float4 PSMain(VS_OUT input) : SV_Target {
    float2 uv = input.uv;
    float2 texel = float2(1.0 / u_resolution_x, 1.0 / u_resolution_y);
    float3 col = scene.Sample(samp, uv).rgb;

    float3 bloom = float3(0, 0, 0);
    float weights[5] = { 0.227027, 0.194596, 0.121622, 0.054054, 0.016216 };
    for (int i = 0; i < 5; i++) {
        float off = float(i) * 1.5;
        bloom += scene.Sample(samp, uv + float2(texel.x * off, 0)).rgb * weights[i];
        bloom += scene.Sample(samp, uv - float2(texel.x * off, 0)).rgb * weights[i];
        bloom += scene.Sample(samp, uv + float2(0, texel.y * off)).rgb * weights[i];
        bloom += scene.Sample(samp, uv - float2(0, texel.y * off)).rgb * weights[i];
    }
    bloom *= 0.25;

    float lum = dot(bloom, float3(0.299, 0.587, 0.114));
    float threshold = 0.5;
    bloom *= smoothstep(threshold, threshold + 0.2, lum);
    col += bloom * u_intensity;
    return float4(saturate(col), 1.0);
}
)";

static const char* kHLSL_Scanlines = R"(
cbuffer PostFX : register(b0) {
    float u_time;
    float u_resolution_x;
    float u_resolution_y;
    float u_intensity;
    float u_custom[8];
};

Texture2D scene : register(t0);
SamplerState samp : register(s0);

struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VS_OUT VSMain(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv.y = 1.0 - o.uv.y;
    return o;
}

float4 PSMain(VS_OUT input) : SV_Target {
    float2 uv = input.uv;
    float3 col = scene.Sample(samp, uv).rgb;
    float gap = max(u_custom[0], 2.0);
    float scanline = sin(uv.y * u_resolution_y * 3.14159 / gap) * 0.5 + 0.5;
    col *= lerp(1.0, scanline, 0.3 * u_intensity);
    return float4(col, 1.0);
}
)";

static const char* kHLSL_ChromaticAberration = R"(
cbuffer PostFX : register(b0) {
    float u_time;
    float u_resolution_x;
    float u_resolution_y;
    float u_intensity;
    float u_custom[8];
};

Texture2D scene : register(t0);
SamplerState samp : register(s0);

struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VS_OUT VSMain(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv.y = 1.0 - o.uv.y;
    return o;
}

float4 PSMain(VS_OUT input) : SV_Target {
    float2 uv = input.uv;
    float offset = u_intensity * 0.005;
    float2 dir = uv - 0.5;
    float r = scene.Sample(samp, uv + dir * offset).r;
    float g = scene.Sample(samp, uv).g;
    float b = scene.Sample(samp, uv - dir * offset).b;
    return float4(r, g, b, 1.0);
}
)";

static const char* kHLSL_Vignette = R"(
cbuffer PostFX : register(b0) {
    float u_time;
    float u_resolution_x;
    float u_resolution_y;
    float u_intensity;
    float u_custom[8];
};

Texture2D scene : register(t0);
SamplerState samp : register(s0);

struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VS_OUT VSMain(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv.y = 1.0 - o.uv.y;
    return o;
}

float4 PSMain(VS_OUT input) : SV_Target {
    float2 uv = input.uv;
    float3 col = scene.Sample(samp, uv).rgb;
    float2 center = uv - 0.5;
    float dist = dot(center, center);
    float vignette = 1.0 - dist * 2.0 * u_intensity;
    col *= saturate(vignette);
    return float4(col, 1.0);
}
)";

static const char* kHLSL_Retro = R"(
cbuffer PostFX : register(b0) {
    float u_time;
    float u_resolution_x;
    float u_resolution_y;
    float u_intensity;
    float u_custom[8];
};

Texture2D scene : register(t0);
SamplerState samp : register(s0);

struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VS_OUT VSMain(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv.y = 1.0 - o.uv.y;
    return o;
}

float4 PSMain(VS_OUT input) : SV_Target {
    float2 uv = input.uv;
    float pixelSize = lerp(1.0, 8.0, u_intensity);
    float2 pixelated = floor(uv * float2(u_resolution_x, u_resolution_y) / pixelSize)
                       * pixelSize / float2(u_resolution_x, u_resolution_y);
    float3 col = scene.Sample(samp, pixelated).rgb;
    float levels = lerp(256.0, 8.0, u_intensity);
    col = floor(col * levels) / levels;
    return float4(col, 1.0);
}
)";

static const char* kHLSL_Matrix = R"(
cbuffer PostFX : register(b0) {
    float u_time;
    float u_resolution_x;
    float u_resolution_y;
    float u_intensity;
    float u_custom[8];
};

Texture2D scene : register(t0);
SamplerState samp : register(s0);

struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VS_OUT VSMain(uint id : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv.y = 1.0 - o.uv.y;
    return o;
}

float hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float4 PSMain(VS_OUT input) : SV_Target {
    float2 uv = input.uv;
    float3 col = scene.Sample(samp, uv).rgb;
    float lum = dot(col, float3(0.299, 0.587, 0.114));
    float3 green = float3(0.0, lum, 0.0);
    col = lerp(col, green, 0.7 * u_intensity);

    float cellX = floor(uv.x * u_resolution_x / 10.0);
    float speed = hash(float2(cellX, 0.0)) * 2.0 + 0.5;
    float rain = frac(uv.y + u_time * speed + hash(float2(cellX, 1.0)));
    float drop = smoothstep(0.0, 0.1, rain) * smoothstep(1.0, 0.3, rain);
    col += float3(0.0, drop * 0.15 * u_intensity, 0.0);
    return float4(col, 1.0);
}
)";

// ---------------------------------------------------------------------------
// GLSL post-processing shaders (OpenGL 3.3 core)
// ---------------------------------------------------------------------------

static const char* kGLSL_CRT = R"(
#version 330 core

uniform float u_time;
uniform float u_resolution_x;
uniform float u_resolution_y;
uniform float u_intensity;
uniform float u_custom[8];

uniform sampler2D u_scene;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 uv = v_uv;
    vec2 center = uv - 0.5;
    float r2 = dot(center, center);
    float barrel = 1.0 + r2 * 0.3 * u_intensity;
    vec2 distorted = center * barrel + 0.5;

    if (distorted.x < 0.0 || distorted.x > 1.0 ||
        distorted.y < 0.0 || distorted.y > 1.0) {
        fragColor = vec4(0, 0, 0, 1);
        return;
    }

    vec3 col = texture(u_scene, distorted).rgb;
    float scanline = sin(distorted.y * u_resolution_y * 3.14159) * 0.5 + 0.5;
    col *= mix(1.0, scanline, 0.15 * u_intensity);
    float phosphor = 1.0 + 0.03 * sin(distorted.x * u_resolution_x * 3.14159 * 3.0);
    col *= phosphor;
    float vignette = 1.0 - r2 * 1.5 * u_intensity;
    col *= clamp(vignette, 0.0, 1.0);
    fragColor = vec4(col, 1.0);
}
)";

static const char* kGLSL_Bloom = R"(
#version 330 core

uniform float u_time;
uniform float u_resolution_x;
uniform float u_resolution_y;
uniform float u_intensity;
uniform float u_custom[8];

uniform sampler2D u_scene;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 uv = v_uv;
    vec2 texel = vec2(1.0 / u_resolution_x, 1.0 / u_resolution_y);
    vec3 col = texture(u_scene, uv).rgb;

    vec3 bloom = vec3(0.0);
    float weights[5] = float[5](0.227027, 0.194596, 0.121622, 0.054054, 0.016216);
    for (int i = 0; i < 5; i++) {
        float off = float(i) * 1.5;
        bloom += texture(u_scene, uv + vec2(texel.x * off, 0)).rgb * weights[i];
        bloom += texture(u_scene, uv - vec2(texel.x * off, 0)).rgb * weights[i];
        bloom += texture(u_scene, uv + vec2(0, texel.y * off)).rgb * weights[i];
        bloom += texture(u_scene, uv - vec2(0, texel.y * off)).rgb * weights[i];
    }
    bloom *= 0.25;

    float lum = dot(bloom, vec3(0.299, 0.587, 0.114));
    float threshold = 0.5;
    bloom *= smoothstep(threshold, threshold + 0.2, lum);
    col += bloom * u_intensity;
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)";

static const char* kGLSL_Scanlines = R"(
#version 330 core

uniform float u_time;
uniform float u_resolution_x;
uniform float u_resolution_y;
uniform float u_intensity;
uniform float u_custom[8];

uniform sampler2D u_scene;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 uv = v_uv;
    vec3 col = texture(u_scene, uv).rgb;
    float gap = max(u_custom[0], 2.0);
    float scanline = sin(uv.y * u_resolution_y * 3.14159 / gap) * 0.5 + 0.5;
    col *= mix(1.0, scanline, 0.3 * u_intensity);
    fragColor = vec4(col, 1.0);
}
)";

static const char* kGLSL_ChromaticAberration = R"(
#version 330 core

uniform float u_time;
uniform float u_resolution_x;
uniform float u_resolution_y;
uniform float u_intensity;
uniform float u_custom[8];

uniform sampler2D u_scene;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 uv = v_uv;
    float offset = u_intensity * 0.005;
    vec2 dir = uv - 0.5;
    float r = texture(u_scene, uv + dir * offset).r;
    float g = texture(u_scene, uv).g;
    float b = texture(u_scene, uv - dir * offset).b;
    fragColor = vec4(r, g, b, 1.0);
}
)";

static const char* kGLSL_Vignette = R"(
#version 330 core

uniform float u_time;
uniform float u_resolution_x;
uniform float u_resolution_y;
uniform float u_intensity;
uniform float u_custom[8];

uniform sampler2D u_scene;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 uv = v_uv;
    vec3 col = texture(u_scene, uv).rgb;
    vec2 center = uv - 0.5;
    float dist = dot(center, center);
    float vignette = 1.0 - dist * 2.0 * u_intensity;
    col *= clamp(vignette, 0.0, 1.0);
    fragColor = vec4(col, 1.0);
}
)";

static const char* kGLSL_Retro = R"(
#version 330 core

uniform float u_time;
uniform float u_resolution_x;
uniform float u_resolution_y;
uniform float u_intensity;
uniform float u_custom[8];

uniform sampler2D u_scene;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec2 uv = v_uv;
    float pixelSize = mix(1.0, 8.0, u_intensity);
    vec2 pixelated = floor(uv * vec2(u_resolution_x, u_resolution_y) / pixelSize)
                     * pixelSize / vec2(u_resolution_x, u_resolution_y);
    vec3 col = texture(u_scene, pixelated).rgb;
    float levels = mix(256.0, 8.0, u_intensity);
    col = floor(col * levels) / levels;
    fragColor = vec4(col, 1.0);
}
)";

static const char* kGLSL_Matrix = R"(
#version 330 core

uniform float u_time;
uniform float u_resolution_x;
uniform float u_resolution_y;
uniform float u_intensity;
uniform float u_custom[8];

uniform sampler2D u_scene;

in vec2 v_uv;
out vec4 fragColor;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv = v_uv;
    vec3 col = texture(u_scene, uv).rgb;
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    vec3 green = vec3(0.0, lum, 0.0);
    col = mix(col, green, 0.7 * u_intensity);

    float cellX = floor(uv.x * u_resolution_x / 10.0);
    float speed = hash(vec2(cellX, 0.0)) * 2.0 + 0.5;
    float rain = fract(uv.y + u_time * speed + hash(vec2(cellX, 1.0)));
    float drop = smoothstep(0.0, 0.1, rain) * smoothstep(1.0, 0.3, rain);
    col += vec3(0.0, drop * 0.15 * u_intensity, 0.0);
    fragColor = vec4(col, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string getBuiltinShaderSourceHLSL(BuiltinShader shader) {
    switch (shader) {
        case BuiltinShader::None:                return {};
        case BuiltinShader::CRT:                 return kHLSL_CRT;
        case BuiltinShader::Bloom:               return kHLSL_Bloom;
        case BuiltinShader::Scanlines:           return kHLSL_Scanlines;
        case BuiltinShader::ChromaticAberration: return kHLSL_ChromaticAberration;
        case BuiltinShader::Vignette:            return kHLSL_Vignette;
        case BuiltinShader::Retro:               return kHLSL_Retro;
        case BuiltinShader::Matrix:              return kHLSL_Matrix;
    }
    return {};
}

std::string getBuiltinShaderSourceGLSL(BuiltinShader shader) {
    switch (shader) {
        case BuiltinShader::None:                return {};
        case BuiltinShader::CRT:                 return kGLSL_CRT;
        case BuiltinShader::Bloom:               return kGLSL_Bloom;
        case BuiltinShader::Scanlines:           return kGLSL_Scanlines;
        case BuiltinShader::ChromaticAberration: return kGLSL_ChromaticAberration;
        case BuiltinShader::Vignette:            return kGLSL_Vignette;
        case BuiltinShader::Retro:               return kGLSL_Retro;
        case BuiltinShader::Matrix:              return kGLSL_Matrix;
    }
    return {};
}

std::string getBuiltinShaderSource(BuiltinShader shader) {
#if defined(_WIN32)
    return getBuiltinShaderSourceHLSL(shader);
#else
    return getBuiltinShaderSourceGLSL(shader);
#endif
}

} // namespace termcore
