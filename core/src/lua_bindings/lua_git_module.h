// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_git_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <memory>
#include <string>

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
    // Stored as shared_ptr<void> to avoid sol.hpp in header
    std::shared_ptr<void> branchChangeFn_;
    std::shared_ptr<void> formatBranchFn_;
};

} // namespace termcore
