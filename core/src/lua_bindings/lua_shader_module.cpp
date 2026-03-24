// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_shader_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_shader_module.h"
#include "termcore/shader_effect.h"

namespace termcore {

LuaShaderModule::LuaShaderModule(ShaderEffect* shaderEffect)
    : shaderEffect_(shaderEffect) {}

void LuaShaderModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    sol::state* luaPtr = &lua;
    (void)luaPtr;

    auto shader = terminal.create_named("shader");

    // terminal.shader.enable("CRT", 0.5)
    shader.set_function("enable",
        [this](std::string name, float intensity) {
            if (shaderEffect_) {
                shaderEffect_->setEnabled(name, intensity);
            }
        });

    // terminal.shader.disable("Bloom")
    shader.set_function("disable",
        [this](std::string name) {
            if (shaderEffect_) {
                shaderEffect_->setDisabled(name);
            }
        });

    // terminal.shader.set_param("CRT", "curvature", 0.3)
    shader.set_function("set_param",
        [this](std::string shaderName, std::string key, float value) {
            if (shaderEffect_) {
                shaderEffect_->setCustomParam(shaderName, key, value);
            }
        });

    // terminal.shader.on_frame(function(time) return {intensity=0.5} end)
    shader.set_function("on_frame",
        [this](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            if (shaderEffect_) {
                shaderEffect_->onFrameCallback = [luaFn](float time) {
                    (*luaFn)(time);
                };
            }
        });
}

void LuaShaderModule::clearCallbacks() {
    if (shaderEffect_) {
        shaderEffect_->onFrameCallback = nullptr;
    }
}

} // namespace termcore
