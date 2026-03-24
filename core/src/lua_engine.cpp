#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <termcore/lua_engine.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <unordered_map>

namespace termcore {

static const char* eventToString(LuaEvent event) {
    switch (event) {
    case LuaEvent::OnBell:
        return "bell";
    case LuaEvent::OnTitleChange:
        return "title_change";
    case LuaEvent::OnDirectoryChange:
        return "directory_change";
    case LuaEvent::OnNotification:
        return "notification";
    case LuaEvent::OnProcessExit:
        return "process_exit";
    case LuaEvent::OnResize:
        return "resize";
    case LuaEvent::OnKeyPress:
        return "key_press";
    }
    return "unknown";
}

struct LuaEngine::Impl {
    sol::state lua;
    std::unordered_map<std::string, std::vector<sol::protected_function>>
        event_handlers;
    sol::table terminal_table;

    Impl() {
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table,
                           sol::lib::math);

        // Remove dangerous functions that allow arbitrary code execution
        lua["load"] = sol::nil;
        lua["loadstring"] = sol::nil;
        lua["dofile"] = sol::nil;
        lua["loadfile"] = sol::nil;
        lua["rawget"] = sol::nil;
        lua["rawset"] = sol::nil;

        // Set execution limit to prevent infinite loops
        lua_sethook(lua.lua_state(), [](lua_State* L, lua_Debug*) {
            luaL_error(L, "execution limit exceeded");
        }, LUA_MASKCOUNT, 1000000);

        terminal_table = lua.create_named_table("terminal");
        terminal_table["version"] = "0.1.0";

        terminal_table.set_function(
            "on",
            [this](const std::string& event_name, sol::protected_function fn) {
                event_handlers[event_name].push_back(std::move(fn));
            });

        terminal_table.set_function("log",
                                    [](const std::string& msg) {
                                        // Could route to a real logger later
                                    });
    }
};

LuaEngine::LuaEngine() : impl_(std::make_unique<Impl>()) {}

LuaEngine::~LuaEngine() {
    clearAllModules();
}

bool LuaEngine::isValid() const {
    return impl_ != nullptr;
}

Result<void> LuaEngine::loadPlugin(const std::string& path) {
    auto result = impl_->lua.safe_script_file(path, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        last_error_ = err.what();
        return Error(last_error_);
    }
    loaded_plugins_.push_back(path);
    return {};
}

Result<void> LuaEngine::loadString(const std::string& code) {
    auto result = impl_->lua.safe_script(code, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        last_error_ = err.what();
        return Error(last_error_);
    }
    return {};
}

void LuaEngine::fireEvent(LuaEvent event, const std::string& data) {
    const char* name = eventToString(event);
    auto it = impl_->event_handlers.find(name);
    if (it == impl_->event_handlers.end()) {
        return;
    }
    for (auto& handler : it->second) {
        auto result = handler(data);
        if (!result.valid()) {
            sol::error err = result;
            last_error_ = err.what();
        }
    }
}

void LuaEngine::registerFunction(
    const std::string& name,
    std::function<std::string(const std::string&)> fn) {
    impl_->terminal_table.set_function(name, std::move(fn));
}

void LuaEngine::registerModule(std::shared_ptr<ILuaModule> module) {
    modules_.push_back(std::move(module));
}

void LuaEngine::initializeModules() {
    for (auto& mod : modules_) {
        mod->registerBindings(
            static_cast<void*>(&impl_->lua),
            static_cast<void*>(&impl_->terminal_table));
    }
}

void LuaEngine::initializeModules(const std::vector<PluginCapability>& capabilities) {
    for (auto& mod : modules_) {
        auto required = mod->requiredCapability();
        bool allowed = false;
        for (auto cap : capabilities) {
            if (cap == required) { allowed = true; break; }
        }
        if (allowed) {
            mod->registerBindings(
                static_cast<void*>(&impl_->lua),
                static_cast<void*>(&impl_->terminal_table));
        }
    }
}

void LuaEngine::clearAllModules() {
    for (auto& mod : modules_) {
        mod->clearCallbacks();
    }
    modules_.clear();
}

} // namespace termcore
