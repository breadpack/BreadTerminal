#ifndef TERMCORE_SHADER_EFFECT_H
#define TERMCORE_SHADER_EFFECT_H

#include <string>
#include <unordered_map>

namespace termcore {

/// Configuration for a shader effect.
struct ShaderConfig {
    std::string name;
    std::string source_path;
    bool enabled = true;
    float intensity = 1.0f;
    std::unordered_map<std::string, float> params;
};

/// Uniform data passed to post-processing shaders each frame.
struct ShaderUniforms {
    float time = 0.0f;
    float resolution_x = 0.0f;
    float resolution_y = 0.0f;
    float intensity = 1.0f;
    float custom[8] = {};
};

/// Built-in post-processing shader effects.
enum class BuiltinShader {
    None,
    CRT,
    Bloom,
    Scanlines,
    ChromaticAberration,
    Vignette,
    Retro,
    Matrix
};

/// Convert a BuiltinShader enum to its string name.
std::string builtinShaderToString(BuiltinShader shader);

/// Parse a string name to a BuiltinShader enum.
/// Returns BuiltinShader::None if unrecognized.
BuiltinShader builtinShaderFromString(const std::string& name);

/// Get the HLSL source for a built-in shader effect.
/// Returns empty string for BuiltinShader::None.
std::string getBuiltinShaderSourceHLSL(BuiltinShader shader);

/// Get the GLSL source for a built-in shader effect.
/// Returns empty string for BuiltinShader::None.
std::string getBuiltinShaderSourceGLSL(BuiltinShader shader);

/// Get the shader source for the current platform (HLSL on Windows, GLSL elsewhere).
std::string getBuiltinShaderSource(BuiltinShader shader);

/// Basic syntax validation for shader source code.
/// Returns true if the source appears valid; sets error message on failure.
bool validateShaderSource(const std::string& source, std::string& error);

/// Set up ShaderUniforms from a ShaderConfig and frame parameters.
ShaderUniforms buildShaderUniforms(const ShaderConfig& config,
                                   float time,
                                   float resolution_x,
                                   float resolution_y);

} // namespace termcore

#endif // TERMCORE_SHADER_EFFECT_H
