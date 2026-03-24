#include "termcore/shader_effect.h"
#include <algorithm>
#include <cctype>

namespace termcore {

std::string builtinShaderToString(BuiltinShader shader) {
    switch (shader) {
        case BuiltinShader::None:                return "none";
        case BuiltinShader::CRT:                 return "crt";
        case BuiltinShader::Bloom:               return "bloom";
        case BuiltinShader::Scanlines:           return "scanlines";
        case BuiltinShader::ChromaticAberration: return "chromatic_aberration";
        case BuiltinShader::Vignette:            return "vignette";
        case BuiltinShader::Retro:               return "retro";
        case BuiltinShader::Matrix:              return "matrix";
    }
    return "none";
}

BuiltinShader builtinShaderFromString(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "crt")                  return BuiltinShader::CRT;
    if (lower == "bloom")                return BuiltinShader::Bloom;
    if (lower == "scanlines")            return BuiltinShader::Scanlines;
    if (lower == "chromatic_aberration") return BuiltinShader::ChromaticAberration;
    if (lower == "vignette")             return BuiltinShader::Vignette;
    if (lower == "retro")                return BuiltinShader::Retro;
    if (lower == "matrix")               return BuiltinShader::Matrix;
    return BuiltinShader::None;
}

bool validateShaderSource(const std::string& source, std::string& error) {
    if (source.empty()) {
        error = "Shader source is empty";
        return false;
    }

    // Check for an entry point function (GLSL uses "main", HLSL uses "PSMain"/"VSMain")
    if (source.find("main") == std::string::npos &&
        source.find("Main") == std::string::npos) {
        error = "Shader source missing 'main' function";
        return false;
    }

    // Check balanced braces
    int braceCount = 0;
    for (char c : source) {
        if (c == '{') ++braceCount;
        if (c == '}') --braceCount;
        if (braceCount < 0) {
            error = "Unbalanced braces: unexpected '}'";
            return false;
        }
    }
    if (braceCount != 0) {
        error = "Unbalanced braces: " + std::to_string(braceCount) + " unclosed '{'";
        return false;
    }

    // Check balanced parentheses
    int parenCount = 0;
    for (char c : source) {
        if (c == '(') ++parenCount;
        if (c == ')') --parenCount;
        if (parenCount < 0) {
            error = "Unbalanced parentheses: unexpected ')'";
            return false;
        }
    }
    if (parenCount != 0) {
        error = "Unbalanced parentheses: " + std::to_string(parenCount) + " unclosed '('";
        return false;
    }

    error.clear();
    return true;
}

ShaderUniforms buildShaderUniforms(const ShaderConfig& config,
                                   float time,
                                   float resolution_x,
                                   float resolution_y) {
    ShaderUniforms u;
    u.time = time;
    u.resolution_x = resolution_x;
    u.resolution_y = resolution_y;
    u.intensity = config.intensity;

    // Map named params into custom[0..7] in alphabetical order
    // This provides a stable mapping for shader authors.
    std::vector<std::pair<std::string, float>> sorted(config.params.begin(),
                                                       config.params.end());
    std::sort(sorted.begin(), sorted.end());

    for (size_t i = 0; i < sorted.size() && i < 8; ++i) {
        u.custom[i] = sorted[i].second;
    }

    return u;
}

void ShaderEffect::setEnabled(const std::string& name, float intensity) {
    auto& cfg = configs_[name];
    cfg.name = name;
    cfg.enabled = true;
    cfg.intensity = intensity;
}

void ShaderEffect::setDisabled(const std::string& name) {
    auto it = configs_.find(name);
    if (it != configs_.end()) {
        it->second.enabled = false;
    }
}

void ShaderEffect::setCustomParam(const std::string& shader, const std::string& key, float value) {
    auto& cfg = configs_[shader];
    cfg.name = shader;
    cfg.params[key] = value;
}

void ShaderEffect::dispatchFrame(float time) {
    if (onFrameCallback) {
        onFrameCallback(time);
    }
}

} // namespace termcore
