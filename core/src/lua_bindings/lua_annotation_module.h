// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_annotation_module.h
#pragma once

#include "termcore/lua_module.h"
#include <memory>
#include <string>
#include <vector>

namespace termcore {

class AnnotationManager;

class LuaAnnotationModule : public ILuaModule {
public:
    explicit LuaAnnotationModule(AnnotationManager* annotMgr);

    std::string_view moduleName() const override { return "annotation"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::PaneRead;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    AnnotationManager* annotMgr_;
    // Keep pattern callbacks alive alongside the module (stored as shared_ptr<void> to avoid sol.hpp in header)
    std::vector<std::shared_ptr<void>> patternFns_;
};

} // namespace termcore
