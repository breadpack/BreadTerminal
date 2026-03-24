// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_git_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_git_module.h"
#include "termcore/git_branch_detector.h"

namespace termcore {

LuaGitModule::LuaGitModule(GitBranchDetector* detector)
    : detector_(detector) {}

void LuaGitModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto git = terminal.create_named("git");

    // terminal.git.set_cache_ttl(seconds)
    git.set_function("set_cache_ttl",
        [this](int seconds) {
            if (detector_) {
                detector_->setCacheTtlSeconds(seconds);
            }
        });

    // terminal.git.on_branch_change(function(branch) end)
    git.set_function("on_branch_change",
        [this](sol::protected_function fn) {
            branchChangeFn_ = std::make_shared<sol::protected_function>(std::move(fn));
            if (detector_) {
                auto fnPtr = branchChangeFn_;
                detector_->onBranchChange = [fnPtr](const std::string& branch) {
                    if (fnPtr && fnPtr->valid()) {
                        (*fnPtr)(branch);
                    }
                };
            }
        });

    // terminal.git.format_branch(function(name) return "branch: " .. name end)
    git.set_function("format_branch",
        [this](sol::protected_function fn) {
            formatBranchFn_ = std::make_shared<sol::protected_function>(std::move(fn));
            if (detector_) {
                auto fnPtr = formatBranchFn_;
                detector_->formatBranch = [fnPtr](const std::string& name) -> std::string {
                    if (fnPtr && fnPtr->valid()) {
                        auto result = (*fnPtr)(name);
                        if (result.valid()) {
                            sol::object val = result;
                            if (val.is<std::string>()) {
                                return val.as<std::string>();
                            }
                        }
                    }
                    return name;
                };
            }
        });
}

void LuaGitModule::clearCallbacks() {
    branchChangeFn_.reset();
    formatBranchFn_.reset();
    if (detector_) {
        detector_->onBranchChange = nullptr;
        detector_->formatBranch = nullptr;
    }
}

} // namespace termcore
