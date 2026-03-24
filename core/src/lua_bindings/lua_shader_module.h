// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_shader_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>

namespace termcore {

class ShaderEffect;

class LuaShaderModule : public ILuaModule {
public:
    explicit LuaShaderModule(ShaderEffect* shaderEffect);

    std::string_view moduleName() const override { return "shader"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Config;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    ShaderEffect* shaderEffect_;
};

} // namespace termcore
