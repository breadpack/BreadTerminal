// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_url_module.h
#pragma once

#include "termcore/lua_module.h"
#include <string>

namespace termcore {

class UrlDetector;
class UrlHighlightManager;

class LuaUrlModule : public ILuaModule {
public:
    LuaUrlModule(UrlDetector* detector, UrlHighlightManager* highlight);

    std::string_view moduleName() const override { return "url"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    UrlDetector* detector_;
    UrlHighlightManager* highlight_;
};

} // namespace termcore
