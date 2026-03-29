#if TERMCORE_HAS_LUA

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "termcore/plugin_manager.h"

#include <algorithm>
#include <filesystem>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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
        {"ui", PluginCapability::UI},
        {"clipboard", PluginCapability::Clipboard},
        {"networking", PluginCapability::Networking},
};

} // anonymous namespace

PluginManager::PluginManager(LuaEngine& lua) : lua_(lua) {}

void PluginManager::scanDirectory(const std::string& plugins_dir) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(plugins_dir, ec)) {
        return;
    }

    // Collect names of directory-based plugins so single-file plugins
    // don't conflict (directory takes precedence).
    std::vector<std::string> dir_plugin_names;

    // Phase 1: scan subdirectories for directory-based plugins.
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
            dir_plugin_names.push_back(entry.path().filename().string());
            continue;
        }

        auto result = parseMetadata(dir);
        if (result.ok()) {
            PluginInfo info;
            info.metadata = std::move(result.value());
            info.state = PluginState::Discovered;
            info.directory = dir;
            dir_plugin_names.push_back(entry.path().filename().string());
            plugins_.push_back(std::move(info));
        } else {
            PluginInfo info;
            info.metadata.name = entry.path().filename().string();
            info.state = PluginState::Error;
            info.error_message = result.errorMessage();
            info.directory = dir;
            dir_plugin_names.push_back(entry.path().filename().string());
            plugins_.push_back(std::move(info));
        }
    }

    // Phase 2: scan for single-file .lua plugins.
    for (const auto& entry : fs::directory_iterator(plugins_dir, ec)) {
        if (entry.is_directory()) {
            continue;
        }

        const auto& path = entry.path();
        if (path.extension() != ".lua") {
            continue;
        }

        // Derive plugin name from filename without extension.
        auto stem = path.stem().string();

        // If a directory-based plugin with the same name exists, skip.
        auto dir_it = std::find(dir_plugin_names.begin(),
                                dir_plugin_names.end(), stem);
        if (dir_it != dir_plugin_names.end()) {
            continue;
        }

        // Skip if already discovered.
        auto file_str = path.string();
        auto it = std::find_if(plugins_.begin(), plugins_.end(),
                               [&file_str](const PluginInfo& p) {
                                   return p.metadata.entry_file == file_str;
                               });
        if (it != plugins_.end()) {
            continue;
        }

        auto result = parseSingleFileMetadata(file_str);
        if (result.ok()) {
            PluginInfo info;
            info.metadata = std::move(result.value());
            info.state = PluginState::Discovered;
            info.directory = plugins_dir;  // parent directory
            plugins_.push_back(std::move(info));
        } else {
            PluginInfo info;
            info.metadata.name = stem;
            info.metadata.entry_file = file_str;
            info.state = PluginState::Error;
            info.error_message = result.errorMessage();
            info.directory = plugins_dir;
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

    // For single-file plugins, entry_file IS the plugin code.
    // For directory-based plugins, entry_file points to init.lua.
    auto result = lua_.loadPlugin(it->metadata.entry_file);

    // Restore sandboxed globals so other plugins aren't affected.
    restoreSandbox(it->metadata);

    if (!result.ok()) {
        it->state = PluginState::Error;
        it->error_message = result.errorMessage();
        return result;
    }

    // Call setup() if the plugin defines it.
    lua_.loadString(
        "if type(setup) == 'function' then setup({}) end");

    // Store on_unload reference if defined.
    auto sanitized = sanitizeName(name);
    lua_.loadString(
        "if type(on_unload) == 'function' then "
        "  __bt_plugin_unload_" + sanitized + " = on_unload "
        "end");

    it->state = PluginState::Loaded;
    return {};
}

void PluginManager::unloadPlugin(const std::string& name) {
    auto it = std::find_if(plugins_.begin(), plugins_.end(),
                           [&name](const PluginInfo& p) {
                               return p.metadata.name == name;
                           });
    if (it == plugins_.end()) {
        return;
    }

    // Call on_unload if defined.
    auto sanitized = sanitizeName(name);
    lua_.loadString(
        "if __bt_plugin_unload_" + sanitized + " then "
        "  __bt_plugin_unload_" + sanitized + "() "
        "  __bt_plugin_unload_" + sanitized + " = nil "
        "end");

    it->state = PluginState::Disabled;
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

std::string PluginManager::sanitizeName(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (auto ch : name) {
        // Only allow alphanumeric and underscore — reject everything else
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_') {
            result += ch;
        } else {
            result += '_';
        }
    }
    if (result.empty()) result = "_unnamed";
    return result;
}

Result<PluginManager::DepSpec> PluginManager::parseDependency(const std::string& spec) {
    // Format: "name" or "name >= 1.0.0" or "name == 2.0"
    DepSpec dep;

    // Trim whitespace
    auto trimmed = spec;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();

    if (trimmed.empty()) {
        return Error("empty dependency spec");
    }

    // Find operator position
    static const std::string ops[] = {">=", "<=", "==", ">", "<"};
    size_t op_pos = std::string::npos;
    std::string found_op;

    for (const auto& op : ops) {
        auto pos = trimmed.find(op);
        if (pos != std::string::npos && (op_pos == std::string::npos || pos < op_pos)) {
            op_pos = pos;
            found_op = op;
        }
    }

    if (op_pos == std::string::npos) {
        // No operator - just a name, any version
        dep.name = trimmed;
        return dep;
    }

    dep.name = trimmed.substr(0, op_pos);
    dep.op = found_op;
    dep.version = trimmed.substr(op_pos + found_op.size());

    // Trim whitespace from name and version
    while (!dep.name.empty() && dep.name.back() == ' ') dep.name.pop_back();
    while (!dep.version.empty() && dep.version.front() == ' ') dep.version.erase(dep.version.begin());

    if (dep.name.empty()) {
        return Error("empty plugin name in dependency spec: " + spec);
    }

    return dep;
}

bool PluginManager::versionSatisfies(const std::string& actual, const std::string& op, const std::string& required) {
    if (op.empty()) return true;  // Any version
    if (actual.empty()) return true;  // No version info, assume ok

    // Parse semver: split on '.' and compare numerically
    auto parseVersion = [](const std::string& v) -> std::vector<int> {
        std::vector<int> parts;
        std::istringstream stream(v);
        std::string segment;
        while (std::getline(stream, segment, '.')) {
            try {
                parts.push_back(std::stoi(segment));
            } catch (...) {
                parts.push_back(0);
            }
        }
        // Pad to at least 3 components
        while (parts.size() < 3) parts.push_back(0);
        return parts;
    };

    auto a = parseVersion(actual);
    auto r = parseVersion(required);

    // Compare left to right
    auto compare = [](const std::vector<int>& a, const std::vector<int>& b) -> int {
        for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
            int av = (i < a.size()) ? a[i] : 0;
            int bv = (i < b.size()) ? b[i] : 0;
            if (av < bv) return -1;
            if (av > bv) return 1;
        }
        return 0;
    };

    int cmp = compare(a, r);
    if (op == ">=") return cmp >= 0;
    if (op == "<=") return cmp <= 0;
    if (op == "==") return cmp == 0;
    if (op == ">")  return cmp > 0;
    if (op == "<")  return cmp < 0;
    return true;
}

Result<std::vector<std::string>> PluginManager::topologicalSort() const {
    // Build adjacency list and in-degree map
    std::unordered_map<std::string, std::vector<std::string>> adj;  // dep -> dependents
    std::unordered_map<std::string, int> in_degree;
    std::unordered_set<std::string> all_names;

    for (const auto& info : plugins_) {
        const auto& name = info.metadata.name;
        all_names.insert(name);
        if (in_degree.find(name) == in_degree.end()) {
            in_degree[name] = 0;
        }

        // Dependencies: this plugin depends on dep.name
        for (const auto& dep_spec : info.metadata.dependencies) {
            auto dep_result = parseDependency(dep_spec);
            if (!dep_result.ok()) continue;
            const auto& dep_name = dep_result.value().name;

            // Only add edge if the dependency is a known plugin
            auto dep_it = std::find_if(plugins_.begin(), plugins_.end(),
                [&dep_name](const PluginInfo& p) { return p.metadata.name == dep_name; });
            if (dep_it != plugins_.end()) {
                adj[dep_name].push_back(name);
                in_degree[name]++;
            }
        }

        // After: this plugin should load after the named plugins
        for (const auto& after_name : info.metadata.after) {
            auto after_it = std::find_if(plugins_.begin(), plugins_.end(),
                [&after_name](const PluginInfo& p) { return p.metadata.name == after_name; });
            if (after_it != plugins_.end()) {
                adj[after_name].push_back(name);
                in_degree[name]++;
            }
        }
    }

    // Kahn's algorithm
    std::queue<std::string> ready;
    for (const auto& name : all_names) {
        if (in_degree[name] == 0) {
            ready.push(name);
        }
    }

    std::vector<std::string> sorted;
    while (!ready.empty()) {
        auto current = ready.front();
        ready.pop();
        sorted.push_back(current);

        if (adj.find(current) != adj.end()) {
            for (const auto& next : adj[current]) {
                in_degree[next]--;
                if (in_degree[next] == 0) {
                    ready.push(next);
                }
            }
        }
    }

    if (sorted.size() != all_names.size()) {
        // Circular dependency detected - find the cycle
        std::string cycle_plugins;
        for (const auto& [name, degree] : in_degree) {
            if (degree > 0) {
                if (!cycle_plugins.empty()) cycle_plugins += ", ";
                cycle_plugins += name;
            }
        }
        return Error("circular dependency detected among: " + cycle_plugins);
    }

    return sorted;
}

Result<std::vector<std::string>> PluginManager::resolveLoadOrder() const {
    // First check that all declared dependencies exist and version constraints are met
    for (const auto& info : plugins_) {
        for (const auto& dep_spec : info.metadata.dependencies) {
            auto dep_result = parseDependency(dep_spec);
            if (!dep_result.ok()) {
                return Error("invalid dependency spec '" + dep_spec + "' in plugin " + info.metadata.name + ": " + dep_result.errorMessage());
            }

            const auto& dep = dep_result.value();

            // Check if the dependency plugin exists
            auto dep_it = std::find_if(plugins_.begin(), plugins_.end(),
                [&dep](const PluginInfo& p) { return p.metadata.name == dep.name; });
            if (dep_it == plugins_.end()) {
                return Error("plugin '" + info.metadata.name + "' depends on '" + dep.name + "' which is not installed");
            }

            // Check version constraint
            if (!dep.op.empty() && !dep.version.empty()) {
                if (!versionSatisfies(dep_it->metadata.version, dep.op, dep.version)) {
                    return Error("plugin '" + info.metadata.name + "' requires " + dep_spec +
                                 " but found version " + dep_it->metadata.version);
                }
            }
        }
    }

    return topologicalSort();
}

Result<void> PluginManager::loadAll() {
    auto order_result = resolveLoadOrder();
    if (!order_result.ok()) {
        return Error(order_result.errorMessage());
    }

    const auto& order = order_result.value();
    for (const auto& name : order) {
        auto it = std::find_if(plugins_.begin(), plugins_.end(),
            [&name](const PluginInfo& p) { return p.metadata.name == name; });
        if (it == plugins_.end()) continue;

        // If lazy, set state to Lazy and skip loading
        if (it->metadata.lazy) {
            it->state = PluginState::Lazy;
            continue;
        }

        // Skip plugins that are already loaded, active, disabled, or errored
        if (it->state != PluginState::Discovered) continue;

        loadPlugin(name);
    }

    // Register lazy plugin triggers
    registerLazyTriggers();

    return {};
}

void PluginManager::registerLazyTriggers() {
    lazy_event_triggers_.clear();
    lazy_command_triggers_.clear();

    for (const auto& info : plugins_) {
        if (info.state != PluginState::Lazy) continue;

        if (!info.metadata.on_event.empty()) {
            lazy_event_triggers_[info.metadata.on_event].push_back(info.metadata.name);
        }
        if (!info.metadata.on_command.empty()) {
            lazy_command_triggers_[info.metadata.on_command].push_back(info.metadata.name);
        }
    }
}

void PluginManager::checkLazyEvent(const std::string& event_name) {
    auto it = lazy_event_triggers_.find(event_name);
    if (it == lazy_event_triggers_.end()) return;

    // Copy names since loadPlugin may modify state
    auto names = it->second;
    lazy_event_triggers_.erase(it);

    for (const auto& name : names) {
        auto plugin_it = std::find_if(plugins_.begin(), plugins_.end(),
            [&name](const PluginInfo& p) { return p.metadata.name == name; });
        if (plugin_it != plugins_.end() && plugin_it->state == PluginState::Lazy) {
            plugin_it->state = PluginState::Discovered;  // Reset to allow loading
            loadPlugin(name);
        }
    }
}

void PluginManager::checkLazyCommand(const std::string& command_name) {
    auto it = lazy_command_triggers_.find(command_name);
    if (it == lazy_command_triggers_.end()) return;

    auto names = it->second;
    lazy_command_triggers_.erase(it);

    for (const auto& name : names) {
        auto plugin_it = std::find_if(plugins_.begin(), plugins_.end(),
            [&name](const PluginInfo& p) { return p.metadata.name == name; });
        if (plugin_it != plugins_.end() && plugin_it->state == PluginState::Lazy) {
            plugin_it->state = PluginState::Discovered;  // Reset to allow loading
            loadPlugin(name);
        }
    }
}

void PluginManager::parseExtendedMetadata(void* table_ptr, PluginMetadata& meta) {
    auto& t = *static_cast<sol::table*>(table_ptr);

    // Dependencies list
    if (auto deps = t["dependencies"]; deps.valid() && deps.get_type() == sol::type::table) {
        sol::table dep_table = deps;
        for (auto& [k, v] : dep_table) {
            if (v.get_type() == sol::type::string) {
                meta.dependencies.push_back(v.as<std::string>());
            }
        }
    }

    // After list
    if (auto after = t["after"]; after.valid() && after.get_type() == sol::type::table) {
        sol::table after_table = after;
        for (auto& [k, v] : after_table) {
            if (v.get_type() == sol::type::string) {
                meta.after.push_back(v.as<std::string>());
            }
        }
    }

    // Lazy flag
    if (auto lazy = t["lazy"]; lazy.valid() && lazy.get_type() == sol::type::boolean) {
        meta.lazy = lazy.get<bool>();
    }

    // On event trigger
    if (auto ev = t["on_event"]; ev.valid() && ev.get_type() == sol::type::string) {
        meta.on_event = ev.get<std::string>();
    }

    // On command trigger
    if (auto cmd = t["on_command"]; cmd.valid() && cmd.get_type() == sol::type::string) {
        meta.on_command = cmd.get<std::string>();
    }
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

    // Parse extended metadata fields (dependencies, lazy, etc.)
    sol::table t_ref = t;
    parseExtendedMetadata(static_cast<void*>(&t_ref), meta);

    meta.entry_file = (fs::path(plugin_dir) / "init.lua").string();

    return meta;
}

Result<PluginMetadata> PluginManager::parseSingleFileMetadata(
    const std::string& lua_file) {
    namespace fs = std::filesystem;

    // Use a temporary sol state to parse the file safely.
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table);

    auto result = lua.safe_script_file(lua_file, sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        return Error("failed to parse single-file plugin: " +
                     std::string(err.what()));
    }

    PluginMetadata meta;
    auto stem = fs::path(lua_file).stem().string();
    meta.entry_file = lua_file;

    // Check for a global 'plugin' table set during execution.
    sol::object plugin_obj = lua["plugin"];
    if (plugin_obj.valid() && plugin_obj.get_type() == sol::type::table) {
        sol::table t = plugin_obj;

        // Name: use from table if present, else filename.
        if (auto v = t["name"];
            v.valid() && v.get_type() == sol::type::string) {
            meta.name = v.get<std::string>();
        } else {
            meta.name = stem;
        }

        // Optional fields.
        if (auto v = t["version"];
            v.valid() && v.get_type() == sol::type::string) {
            meta.version = v.get<std::string>();
        }
        if (auto v = t["author"];
            v.valid() && v.get_type() == sol::type::string) {
            meta.author = v.get<std::string>();
        }
        if (auto v = t["description"];
            v.valid() && v.get_type() == sol::type::string) {
            meta.description = v.get<std::string>();
        }

        // Capabilities list.
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
                }
            }
        }

        // Parse extended metadata fields (dependencies, lazy, etc.)
        sol::table t_ref = t;
        parseExtendedMetadata(static_cast<void*>(&t_ref), meta);
    } else {
        // No plugin table: use filename as name, grant all capabilities.
        meta.name = stem;
        for (const auto& [key, cap] : kCapabilityMap) {
            meta.capabilities.push_back(cap);
        }
    }

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
Result<std::vector<std::string>> PluginManager::resolveLoadOrder() const {
    return Error("Lua not available");
}
Result<void> PluginManager::loadAll() {
    return Error("Lua not available");
}
void PluginManager::registerLazyTriggers() {}
void PluginManager::checkLazyEvent(const std::string&) {}
void PluginManager::checkLazyCommand(const std::string&) {}
Result<PluginMetadata> PluginManager::parseMetadata(const std::string&) {
    return Error("Lua not available");
}
Result<PluginMetadata> PluginManager::parseSingleFileMetadata(
    const std::string&) {
    return Error("Lua not available");
}
void PluginManager::applySandbox(const PluginMetadata&) {}
void PluginManager::restoreSandbox(const PluginMetadata&) {}
std::string PluginManager::sanitizeName(const std::string& name) {
    std::string result = name;
    for (auto& ch : result) {
        if (ch == '-' || ch == '.' || ch == ' ') ch = '_';
    }
    return result;
}
Result<PluginManager::DepSpec> PluginManager::parseDependency(const std::string&) {
    return Error("Lua not available");
}
bool PluginManager::versionSatisfies(const std::string&, const std::string&, const std::string&) {
    return false;
}
Result<std::vector<std::string>> PluginManager::topologicalSort() const {
    return Error("Lua not available");
}
void PluginManager::parseExtendedMetadata(void*, PluginMetadata&) {}

} // namespace termcore

#endif // TERMCORE_HAS_LUA
