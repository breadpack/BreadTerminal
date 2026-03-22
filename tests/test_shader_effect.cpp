#include <gtest/gtest.h>
#include "termcore/shader_effect.h"
#include "termcore/config.h"

using namespace termcore;

// --- Builtin enum <-> string conversion ---

TEST(ShaderEffect, BuiltinShaderToString) {
    EXPECT_EQ(builtinShaderToString(BuiltinShader::None), "none");
    EXPECT_EQ(builtinShaderToString(BuiltinShader::CRT), "crt");
    EXPECT_EQ(builtinShaderToString(BuiltinShader::Bloom), "bloom");
    EXPECT_EQ(builtinShaderToString(BuiltinShader::Scanlines), "scanlines");
    EXPECT_EQ(builtinShaderToString(BuiltinShader::ChromaticAberration), "chromatic_aberration");
    EXPECT_EQ(builtinShaderToString(BuiltinShader::Vignette), "vignette");
    EXPECT_EQ(builtinShaderToString(BuiltinShader::Retro), "retro");
    EXPECT_EQ(builtinShaderToString(BuiltinShader::Matrix), "matrix");
}

TEST(ShaderEffect, BuiltinShaderFromString) {
    EXPECT_EQ(builtinShaderFromString("crt"), BuiltinShader::CRT);
    EXPECT_EQ(builtinShaderFromString("bloom"), BuiltinShader::Bloom);
    EXPECT_EQ(builtinShaderFromString("scanlines"), BuiltinShader::Scanlines);
    EXPECT_EQ(builtinShaderFromString("chromatic_aberration"), BuiltinShader::ChromaticAberration);
    EXPECT_EQ(builtinShaderFromString("vignette"), BuiltinShader::Vignette);
    EXPECT_EQ(builtinShaderFromString("retro"), BuiltinShader::Retro);
    EXPECT_EQ(builtinShaderFromString("matrix"), BuiltinShader::Matrix);
    EXPECT_EQ(builtinShaderFromString("none"), BuiltinShader::None);
    EXPECT_EQ(builtinShaderFromString("unknown"), BuiltinShader::None);
}

TEST(ShaderEffect, BuiltinShaderFromStringCaseInsensitive) {
    EXPECT_EQ(builtinShaderFromString("CRT"), BuiltinShader::CRT);
    EXPECT_EQ(builtinShaderFromString("Bloom"), BuiltinShader::Bloom);
    EXPECT_EQ(builtinShaderFromString("MATRIX"), BuiltinShader::Matrix);
}

TEST(ShaderEffect, RoundtripStringConversion) {
    // Every builtin shader should round-trip through string conversion.
    BuiltinShader shaders[] = {
        BuiltinShader::None, BuiltinShader::CRT, BuiltinShader::Bloom,
        BuiltinShader::Scanlines, BuiltinShader::ChromaticAberration,
        BuiltinShader::Vignette, BuiltinShader::Retro, BuiltinShader::Matrix
    };
    for (auto s : shaders) {
        std::string name = builtinShaderToString(s);
        EXPECT_EQ(builtinShaderFromString(name), s)
            << "Round-trip failed for: " << name;
    }
}

// --- Builtin shader source retrieval ---

TEST(ShaderEffect, BuiltinSourceNoneIsEmpty) {
    EXPECT_TRUE(getBuiltinShaderSourceHLSL(BuiltinShader::None).empty());
    EXPECT_TRUE(getBuiltinShaderSourceGLSL(BuiltinShader::None).empty());
    EXPECT_TRUE(getBuiltinShaderSource(BuiltinShader::None).empty());
}

TEST(ShaderEffect, BuiltinSourceHLSLNonEmpty) {
    BuiltinShader shaders[] = {
        BuiltinShader::CRT, BuiltinShader::Bloom, BuiltinShader::Scanlines,
        BuiltinShader::ChromaticAberration, BuiltinShader::Vignette,
        BuiltinShader::Retro, BuiltinShader::Matrix
    };
    for (auto s : shaders) {
        std::string src = getBuiltinShaderSourceHLSL(s);
        EXPECT_FALSE(src.empty()) << "HLSL empty for: " << builtinShaderToString(s);
        // HLSL shaders should contain PSMain entry point
        EXPECT_NE(src.find("PSMain"), std::string::npos)
            << "HLSL missing PSMain for: " << builtinShaderToString(s);
    }
}

TEST(ShaderEffect, BuiltinSourceGLSLNonEmpty) {
    BuiltinShader shaders[] = {
        BuiltinShader::CRT, BuiltinShader::Bloom, BuiltinShader::Scanlines,
        BuiltinShader::ChromaticAberration, BuiltinShader::Vignette,
        BuiltinShader::Retro, BuiltinShader::Matrix
    };
    for (auto s : shaders) {
        std::string src = getBuiltinShaderSourceGLSL(s);
        EXPECT_FALSE(src.empty()) << "GLSL empty for: " << builtinShaderToString(s);
        // GLSL shaders should contain a main function
        EXPECT_NE(src.find("void main()"), std::string::npos)
            << "GLSL missing main for: " << builtinShaderToString(s);
        // GLSL shaders should have a version directive
        EXPECT_NE(src.find("#version"), std::string::npos)
            << "GLSL missing #version for: " << builtinShaderToString(s);
    }
}

TEST(ShaderEffect, BuiltinSourceContainsUniforms) {
    // All builtin shaders should reference the standard uniform names
    std::string src = getBuiltinShaderSourceHLSL(BuiltinShader::CRT);
    EXPECT_NE(src.find("u_time"), std::string::npos);
    EXPECT_NE(src.find("u_resolution_x"), std::string::npos);
    EXPECT_NE(src.find("u_intensity"), std::string::npos);

    std::string glsl = getBuiltinShaderSourceGLSL(BuiltinShader::CRT);
    EXPECT_NE(glsl.find("u_time"), std::string::npos);
    EXPECT_NE(glsl.find("u_resolution_x"), std::string::npos);
    EXPECT_NE(glsl.find("u_intensity"), std::string::npos);
}

// --- Shader config parsing ---

TEST(ShaderEffect, ShaderConfigDefaults) {
    ShaderConfig config;
    EXPECT_TRUE(config.name.empty());
    EXPECT_TRUE(config.source_path.empty());
    EXPECT_TRUE(config.enabled);
    EXPECT_FLOAT_EQ(config.intensity, 1.0f);
    EXPECT_TRUE(config.params.empty());
}

TEST(ShaderEffect, ShaderConfigWithParams) {
    ShaderConfig config;
    config.name = "crt";
    config.intensity = 0.75f;
    config.params["scanline_gap"] = 3.0f;
    config.params["barrel_strength"] = 0.5f;

    EXPECT_EQ(config.name, "crt");
    EXPECT_FLOAT_EQ(config.intensity, 0.75f);
    EXPECT_EQ(config.params.size(), 2u);
    EXPECT_FLOAT_EQ(config.params["scanline_gap"], 3.0f);
}

// --- Uniform setup ---

TEST(ShaderEffect, BuildShaderUniformsBasic) {
    ShaderConfig config;
    config.intensity = 0.5f;

    ShaderUniforms u = buildShaderUniforms(config, 1.5f, 1920.0f, 1080.0f);
    EXPECT_FLOAT_EQ(u.time, 1.5f);
    EXPECT_FLOAT_EQ(u.resolution_x, 1920.0f);
    EXPECT_FLOAT_EQ(u.resolution_y, 1080.0f);
    EXPECT_FLOAT_EQ(u.intensity, 0.5f);
    // No params -> custom should be zero
    for (int i = 0; i < 8; ++i) {
        EXPECT_FLOAT_EQ(u.custom[i], 0.0f);
    }
}

TEST(ShaderEffect, BuildShaderUniformsWithParams) {
    ShaderConfig config;
    config.intensity = 1.0f;
    config.params["alpha"] = 0.1f;
    config.params["beta"] = 0.2f;
    config.params["gamma"] = 0.3f;

    ShaderUniforms u = buildShaderUniforms(config, 0.0f, 800.0f, 600.0f);
    // Params are sorted alphabetically: alpha, beta, gamma
    EXPECT_FLOAT_EQ(u.custom[0], 0.1f);
    EXPECT_FLOAT_EQ(u.custom[1], 0.2f);
    EXPECT_FLOAT_EQ(u.custom[2], 0.3f);
    // Remaining slots should be zero
    for (int i = 3; i < 8; ++i) {
        EXPECT_FLOAT_EQ(u.custom[i], 0.0f);
    }
}

TEST(ShaderEffect, BuildShaderUniformsMaxParams) {
    ShaderConfig config;
    config.intensity = 1.0f;
    // Add more than 8 params -- only first 8 (alphabetically) should be used
    for (int i = 0; i < 12; ++i) {
        config.params["p" + std::to_string(i)] = static_cast<float>(i);
    }

    ShaderUniforms u = buildShaderUniforms(config, 0.0f, 100.0f, 100.0f);
    // p0 through p11 sorted alphabetically: p0, p1, p10, p11, p2, p3, p4, p5
    // Only 8 fit in custom[]
    EXPECT_FLOAT_EQ(u.custom[0], 0.0f);  // p0
    EXPECT_FLOAT_EQ(u.custom[1], 1.0f);  // p1
    EXPECT_FLOAT_EQ(u.custom[2], 10.0f); // p10
    EXPECT_FLOAT_EQ(u.custom[3], 11.0f); // p11
}

// --- Shader validation ---

TEST(ShaderEffect, ValidateValidShader) {
    std::string error;
    std::string source = R"(
        void main() {
            float x = 1.0;
        }
    )";
    EXPECT_TRUE(validateShaderSource(source, error));
    EXPECT_TRUE(error.empty());
}

TEST(ShaderEffect, ValidateEmptyShader) {
    std::string error;
    EXPECT_FALSE(validateShaderSource("", error));
    EXPECT_NE(error.find("empty"), std::string::npos);
}

TEST(ShaderEffect, ValidateNoMainFunction) {
    std::string error;
    std::string source = "float x = 1.0;";
    EXPECT_FALSE(validateShaderSource(source, error));
    EXPECT_NE(error.find("main"), std::string::npos);
}

TEST(ShaderEffect, ValidateUnbalancedBraces) {
    std::string error;
    std::string source = "void main() { { }";
    EXPECT_FALSE(validateShaderSource(source, error));
    EXPECT_NE(error.find("brace"), std::string::npos);
}

TEST(ShaderEffect, ValidateUnbalancedParens) {
    std::string error;
    std::string source = "void main(( {}";
    EXPECT_FALSE(validateShaderSource(source, error));
    EXPECT_NE(error.find("parenthes"), std::string::npos);
}

TEST(ShaderEffect, ValidateBuiltinShadersPassValidation) {
    BuiltinShader shaders[] = {
        BuiltinShader::CRT, BuiltinShader::Bloom, BuiltinShader::Scanlines,
        BuiltinShader::ChromaticAberration, BuiltinShader::Vignette,
        BuiltinShader::Retro, BuiltinShader::Matrix
    };
    for (auto s : shaders) {
        std::string error;
        std::string hlsl = getBuiltinShaderSourceHLSL(s);
        EXPECT_TRUE(validateShaderSource(hlsl, error))
            << "HLSL validation failed for " << builtinShaderToString(s)
            << ": " << error;

        std::string glsl = getBuiltinShaderSourceGLSL(s);
        EXPECT_TRUE(validateShaderSource(glsl, error))
            << "GLSL validation failed for " << builtinShaderToString(s)
            << ": " << error;
    }
}

// --- ShaderUniforms default state ---

TEST(ShaderEffect, ShaderUniformsDefaults) {
    ShaderUniforms u;
    EXPECT_FLOAT_EQ(u.time, 0.0f);
    EXPECT_FLOAT_EQ(u.resolution_x, 0.0f);
    EXPECT_FLOAT_EQ(u.resolution_y, 0.0f);
    EXPECT_FLOAT_EQ(u.intensity, 1.0f);
    for (int i = 0; i < 8; ++i) {
        EXPECT_FLOAT_EQ(u.custom[i], 0.0f);
    }
}

// --- Config fields ---

TEST(ShaderEffect, ConfigDefaultShaderFields) {
    Config cfg;
    EXPECT_EQ(cfg.custom_shader, "none");
    EXPECT_FLOAT_EQ(cfg.shader_intensity, 1.0f);
}
