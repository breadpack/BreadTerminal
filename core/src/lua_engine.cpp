#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <termcore/lua_engine.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <filesystem>
#include <unordered_map>

#include "default_config_lua.h"
#include "default_colors_lua.h"
#include "default_keybindings_lua.h"
#include "default_tab_title_lua.h"
#include "default_paste_guard_lua.h"
#include "default_themes_lua.h"
#include "default_providers_lua.h"

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
                           sol::lib::math, sol::lib::coroutine,
                           sol::lib::package);

        // Remove dangerous functions that allow arbitrary code execution
        lua["load"] = sol::nil;
        lua["loadstring"] = sol::nil;
        lua["dofile"] = sol::nil;
        lua["loadfile"] = sol::nil;
        lua["rawget"] = sol::nil;
        lua["rawset"] = sol::nil;

        // Restrict package loaders — only file-based loading from plugins dir
        lua["package"]["loadlib"] = sol::nil;     // no C modules
        lua["package"]["searchpath"] = sol::nil;   // hide search internals

        // Default to empty to prevent loading from system paths.
        // package.path will be set when plugins directory is known via setPluginsPath().
        lua["package"]["path"] = "";
        lua["package"]["cpath"] = "";  // no C modules

        // Set execution limit to prevent infinite loops
        lua_sethook(lua.lua_state(), [](lua_State* L, lua_Debug*) {
            luaL_error(L, "execution limit exceeded");
        }, LUA_MASKCOUNT, 1000000);

        terminal_table = lua.create_named_table("terminal");
        terminal_table["version"] = "0.1.0";

#if defined(__APPLE__)
        terminal_table["platform"] = "macos";
#elif defined(_WIN32)
        terminal_table["platform"] = "windows";
#else
        terminal_table["platform"] = "linux";
#endif

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

void LuaEngine::setActionHandler(ActionHandler handler) {
    impl_->terminal_table.set_function("action",
        [handler = std::move(handler)](const std::string& name) {
            handler(name);
        });
}

namespace {
struct DefaultScript {
    const char* name;
    const unsigned char* data;
    unsigned int len;
};

static const DefaultScript kDefaultScripts[] = {
    {"config",      default_config_lua,      default_config_lua_len},
    {"colors",      default_colors_lua,      default_colors_lua_len},
    {"keybindings", default_keybindings_lua,  default_keybindings_lua_len},
    {"tab_title",   default_tab_title_lua,   default_tab_title_lua_len},
    {"paste_guard", default_paste_guard_lua,  default_paste_guard_lua_len},
    {"themes",      default_themes_lua,       default_themes_lua_len},
    {"providers",   default_providers_lua,    default_providers_lua_len},
};
} // anonymous namespace

void LuaEngine::loadDefaults() {
    for (const auto& script : kDefaultScripts) {
        std::string code(reinterpret_cast<const char*>(script.data), script.len);
        auto result = loadString(code);
        if (!result.ok()) {
            // Log error but continue loading remaining defaults
        }
    }
}

void LuaEngine::setPluginsPath(const std::string& plugins_dir) {
    if (plugins_dir.empty()) return;

    // Validate that module names don't contain path traversal sequences.
    // Override package.searchers to add a pre-check before file search.
    impl_->lua["package"]["path"] = "";

    // Store the allowed base directory for validation
    namespace fs = std::filesystem;
    auto canonical_base = fs::weakly_canonical(fs::path(plugins_dir));

    // Set the actual search path
    std::string path = plugins_dir + "/lib/?.lua;"
                     + plugins_dir + "/lib/?/init.lua;"
                     + plugins_dir + "/?.lua;"
                     + plugins_dir + "/?/init.lua";
    impl_->lua["package"]["path"] = path;

    // Install a custom require guard that rejects dangerous module names
    impl_->lua["__bt_plugins_dir"] = canonical_base.string();
    impl_->lua.safe_script(
        "local original_require = require\n"
        "require = function(modname)\n"
        "  if type(modname) ~= 'string' then\n"
        "    error('require: module name must be a string')\n"
        "  end\n"
        "  if modname:find('%.%.') or modname:find('/') or modname:find('\\\\') then\n"
        "    error('require: invalid module name ' .. modname .. ' (path traversal not allowed)')\n"
        "  end\n"
        "  return original_require(modname)\n"
        "end\n"
    );
}

} // namespace termcore
