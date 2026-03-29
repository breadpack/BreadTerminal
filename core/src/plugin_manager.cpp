#if TERMCORE_HAS_LUA

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "termcore/plugin_manager.h"

#include <algorithm>
#include <filesystem>
#include <unordered_map>

namespace termcore {
namespace {

// Map capability string (from plugin.lua) to enum value.
static const std::unordered_map<std::string, PluginCapability>
    kCapabilityMap = {
        {"events", PluginCapability::Events},
        {"keybindings", PluginCapability::Keybindings},
        {"config", PluginCapability::Config},
        {"notifications", PluginCapability::Notifications},
        {"pane_read", PluginCapability::PaneRead},
        {"pane_write", PluginCapability::PaneWrite},
        {"filesystem", PluginCapability::FileSystem},
};

} // anonymous namespace

PluginManager::PluginManager(LuaEngine& lua) : lua_(lua) {}

void PluginManager::scanDirectory(const std::string& plugins_dir) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(plugins_dir, ec)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(plugins_dir, ec)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto dir = entry.path().string();
        const auto meta_path = (entry.path() / "plugin.lua").string();
        const auto init_path = (entry.path() / "init.lua").string();

        if (!fs::exists(meta_path) || !fs::exists(init_path)) {
            continue;
        }

        // Skip if we already discovered this plugin directory.
        auto it = std::find_if(plugins_.begin(), plugins_.end(),
                               [&dir](const PluginInfo& p) {
                                   return p.directory == dir;
                               });
        if (it != plugins_.end()) {
            continue;
        }

        auto result = parseMetadata(dir);
        if (result.ok()) {
            PluginInfo info;
            info.metadata = std::move(result.value());
            info.state = PluginState::Discovered;
            info.directory = dir;
            plugins_.push_back(std::move(info));
        } else {
            PluginInfo info;
            info.metadata.name = entry.path().filename().string();
            info.state = PluginState::Error;
            info.error_message = result.errorMessage();
            info.directory = dir;
            plugins_.push_back(std::move(info));
        }
    }
}

Result<void> PluginManager::loadPlugin(const std::string& name) {
    auto it = std::find_if(plugins_.begin(), plugins_.end(),
                           [&name](const PluginInfo& p) {
                               return p.metadata.name == name;
                           });
    if (it == plugins_.end()) {
        return Error("plugin not found: " + name);
    }

    if (it->state == PluginState::Loaded || it->state == PluginState::Active) {
        return {};  // Already loaded.
    }

    if (it->state == PluginState::Disabled) {
        return Error("plugin is disabled: " + name);
    }

    // Apply sandbox before executing plugin code, then restore globals.
    applySandbox(it->metadata);

    namespace fs = std::filesystem;
    auto init_path = (fs::path(it->directory) / "init.lua").string();

    auto result = lua_.loadPlugin(init_path);

    // Restore sandboxed globals so other plugins aren't affected.
    restoreSandbox(it->metadata);

    if (!result.ok()) {
        it->state = PluginState::Error;
        it->error_message = result.errorMessage();
        return result;
    }

    it->state = PluginState::Loaded;
    return {};
}

void PluginManager::unloadPlugin(const std::string& name) {
    auto it = std::find_if(plugins_.begin(), plugins_.end(),
                           [&name](const PluginInfo& p) {
                               return p.metadata.name == name;
                           });
    if (it != plugins_.end()) {
        it->state = PluginState::Disabled;
    }
}

const std::vector<PluginInfo>& PluginManager::plugins() const {
    return plugins_;
}

bool PluginManager::hasCapability(const std::string& plugin_name,
                                  PluginCapability cap) const {
    auto it = std::find_if(plugins_.begin(), plugins_.end(),
                           [&plugin_name](const PluginInfo& p) {
                               return p.metadata.name == plugin_name;
                           });
    if (it == plugins_.end()) {
        return false;
    }

    const auto& caps = it->metadata.capabilities;
    return std::find(caps.begin(), caps.end(), cap) != caps.end();
}

Result<PluginMetadata> PluginManager::parseMetadata(
    const std::string& plugin_dir) {
    namespace fs = std::filesystem;
    auto meta_path = (fs::path(plugin_dir) / "plugin.lua").string();

    // Use a temporary sol state to parse the metadata file safely,
    // isolated from the main engine state.
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table);

    auto result = lua.safe_script_file(meta_path, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        return Error("failed to parse plugin.lua: " + std::string(err.what()));
    }

    // plugin.lua should return a table.
    sol::object obj = result;
    if (obj.get_type() != sol::type::table) {
        return Error("plugin.lua must return a table");
    }

    sol::table t = obj;
    PluginMetadata meta;

    // Required: name
    auto name_obj = t["name"];
    if (!name_obj.valid() || name_obj.get_type() != sol::type::string) {
        return Error("plugin.lua: 'name' field is required");
    }
    meta.name = name_obj.get<std::string>();

    // Optional fields
    if (auto v = t["version"]; v.valid() && v.get_type() == sol::type::string) {
        meta.version = v.get<std::string>();
    }
    if (auto v = t["author"]; v.valid() && v.get_type() == sol::type::string) {
        meta.author = v.get<std::string>();
    }
    if (auto v = t["description"];
        v.valid() && v.get_type() == sol::type::string) {
        meta.description = v.get<std::string>();
    }

    // Capabilities list
    if (auto caps = t["capabilities"];
        caps.valid() && caps.get_type() == sol::type::table) {
        sol::table cap_table = caps;
        for (auto& [k, v] : cap_table) {
            if (v.get_type() == sol::type::string) {
                auto cap_str = v.as<std::string>();
                auto found = kCapabilityMap.find(cap_str);
                if (found != kCapabilityMap.end()) {
                    meta.capabilities.push_back(found->second);
                }
                // Unknown capabilities are silently ignored.
            }
        }
    }

    meta.entry_file = (fs::path(plugin_dir) / "init.lua").string();

    return meta;
}

void PluginManager::applySandbox(const PluginMetadata& meta) {
    const auto& caps = meta.capabilities;

    auto hasCap = [&caps](PluginCapability c) {
        return std::find(caps.begin(), caps.end(), c) != caps.end();
    };

    // Save references to globals we might nil out, so we can restore later.
    lua_.loadString(R"(
        __bt_sandbox_backup = __bt_sandbox_backup or {}
        __bt_sandbox_backup.io = io
        if os then
            __bt_sandbox_backup.os_execute = os.execute
            __bt_sandbox_backup.os_remove = os.remove
            __bt_sandbox_backup.os_rename = os.rename
            __bt_sandbox_backup.os_tmpname = os.tmpname
        end
        if terminal then
            __bt_sandbox_backup.send_text = terminal.send_text
            __bt_sandbox_backup.config = terminal.config
            __bt_sandbox_backup.on = terminal.on
            __bt_sandbox_backup.keymap = terminal.keymap
        end
    )");

    // Always restrict dangerous os functions (if os library is available).
    lua_.loadString("if os then os.execute = nil; os.remove = nil; os.rename = nil end");

    // If FileSystem NOT requested: nil out io library functions.
    if (!hasCap(PluginCapability::FileSystem)) {
        lua_.loadString(R"(
            io = nil
            if os then os.tmpname = nil end
        )");
    }

    // If PaneWrite NOT requested: nil out terminal.send_text.
    if (!hasCap(PluginCapability::PaneWrite)) {
        lua_.loadString(R"(
            if terminal and terminal.send_text then
                terminal.send_text = nil
            end
        )");
    }

    // If Config NOT requested: nil out terminal.config setter.
    if (!hasCap(PluginCapability::Config)) {
        lua_.loadString(R"(
            if terminal and terminal.config then
                terminal.config = nil
            end
        )");
    }

    // If Events NOT requested: nil out terminal.on.
    if (!hasCap(PluginCapability::Events)) {
        lua_.loadString(R"(
            if terminal and terminal.on then
                terminal.on = nil
            end
        )");
    }

    // If Keybindings NOT requested: nil out terminal.keymap.
    if (!hasCap(PluginCapability::Keybindings)) {
        lua_.loadString(R"(
            if terminal and terminal.keymap then
                terminal.keymap = nil
            end
        )");
    }
}

void PluginManager::restoreSandbox(const PluginMetadata&) {
    lua_.loadString(R"(
        if __bt_sandbox_backup then
            if os then
                os.execute = __bt_sandbox_backup.os_execute
                os.remove = __bt_sandbox_backup.os_remove
                os.rename = __bt_sandbox_backup.os_rename
                os.tmpname = __bt_sandbox_backup.os_tmpname
            end
            io = __bt_sandbox_backup.io
            if terminal then
                terminal.send_text = __bt_sandbox_backup.send_text
                terminal.config = __bt_sandbox_backup.config
                terminal.on = __bt_sandbox_backup.on
                terminal.keymap = __bt_sandbox_backup.keymap
            end
            __bt_sandbox_backup = nil
        end
    )");
}

} // namespace termcore

#else // !TERMCORE_HAS_LUA

#include "termcore/plugin_manager.h"

namespace termcore {

PluginManager::PluginManager(LuaEngine& lua) : lua_(lua) {}
void PluginManager::scanDirectory(const std::string&) {}
Result<void> PluginManager::loadPlugin(const std::string& name) {
    return Error("Lua not available");
}
void PluginManager::unloadPlugin(const std::string&) {}
const std::vector<PluginInfo>& PluginManager::plugins() const {
    return plugins_;
}
bool PluginManager::hasCapability(const std::string&, PluginCapability) const {
    return false;
}
Result<PluginMetadata> PluginManager::parseMetadata(const std::string&) {
    return Error("Lua not available");
}
void PluginManager::applySandbox(const PluginMetadata&) {}
void PluginManager::restoreSandbox(const PluginMetadata&) {}

} // namespace termcore

#endif // TERMCORE_HAS_LUA
