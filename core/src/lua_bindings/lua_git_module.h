// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_git_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <memory>
#include <string>

namespace sol { class protected_function; }

namespace termcore {

class GitBranchDetector;

class LuaGitModule : public ILuaModule {
public:
    explicit LuaGitModule(GitBranchDetector* detector);

    std::string_view moduleName() const override { return "git"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    GitBranchDetector* detector_;
    std::shared_ptr<sol::protected_function> branchChangeFn_;
    std::shared_ptr<sol::protected_function> formatBranchFn_;
};

} // namespace termcore
