#pragma once

#include <memory>
#include <string_view>
#include <string>
#include "termcore/plugin.h"

namespace termcore {

class ILuaModule {
public:
    virtual ~ILuaModule() = default;

    // Short name used as sub-table key (e.g. "tab" -> terminal.tab)
    virtual std::string_view moduleName() const = 0;

    // Minimum capability a plugin must declare to access this module.
    // Config context (config.lua) bypasses this check.
    virtual PluginCapability requiredCapability() const = 0;

    // Called once during LuaEngine::initializeModules().
    // Implementations create terminal.<moduleName> sub-table and register functions.
    // Parameters use void* to avoid sol.hpp in public header:
    //   luaState  -- pointer to sol::state
    //   terminal  -- pointer to sol::table (the "terminal" global)
    virtual void registerBindings(void* luaState, void* terminalTable) = 0;

    // Called on plugin unload or engine shutdown.
    // Implementations must clear all stored sol::protected_function references
    // and reset any callback slots on the backing C++ component.
    virtual void clearCallbacks() = 0;
};

} // namespace termcore
