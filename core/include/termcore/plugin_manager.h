#pragma once

#include "termcore/lua_engine.h"
#include "termcore/plugin.h"
#include "termcore/result.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

class PluginManager {
public:
    explicit PluginManager(LuaEngine& lua);

    // Discovery: scan directory for plugins.
    // Each plugin is a directory with plugin.lua (metadata) and init.lua (entry).
    void scanDirectory(const std::string& plugins_dir);

    // Load a discovered plugin by name.
    Result<void> loadPlugin(const std::string& name);

    // Unload a plugin by name.
    void unloadPlugin(const std::string& name);

    // Get all discovered plugins.
    const std::vector<PluginInfo>& plugins() const;

    // Check if a capability is granted to a plugin.
    bool hasCapability(const std::string& plugin_name,
                       PluginCapability cap) const;

    // Resolve load order based on dependencies and 'after' constraints.
    // Returns ordered list of plugin names, or error if circular dependency.
    Result<std::vector<std::string>> resolveLoadOrder() const;

    // Load all plugins in dependency order.
    Result<void> loadAll();

    // Register lazy triggers after initial load.
    void registerLazyTriggers();

    // Called when an event fires - check if any lazy plugin should load.
    void checkLazyEvent(const std::string& event_name);

    // Called when a command is invoked - check if any lazy plugin should load.
    void checkLazyCommand(const std::string& command_name);

private:
    // Parse plugin.lua metadata file (directory-based plugins).
    Result<PluginMetadata> parseMetadata(const std::string& plugin_dir);

    // Parse metadata from a single .lua file plugin.
    Result<PluginMetadata> parseSingleFileMetadata(const std::string& lua_file);

    // Apply sandbox restrictions based on granted capabilities.
    void applySandbox(const PluginMetadata& meta);

    // Restore globals that were nil'd by applySandbox.
    void restoreSandbox(const PluginMetadata& meta);

    // Parse dependency spec "name >= 1.0.0" into components.
    struct DepSpec {
        std::string name;
        std::string op;       // ">=", "<=", "==", ">", "<", "" (any)
        std::string version;
    };
    static Result<DepSpec> parseDependency(const std::string& spec);
    static bool versionSatisfies(const std::string& actual, const std::string& op, const std::string& required);

    // Topological sort for load order.
    Result<std::vector<std::string>> topologicalSort() const;

    // Sanitize plugin name for use as Lua global variable name.
    static std::string sanitizeName(const std::string& name);

    // Parse extended metadata fields from a sol::table (shared by both parsers).
    // Uses void* to avoid sol.hpp in public header (actually a sol::table*).
    static void parseExtendedMetadata(void* table, PluginMetadata& meta);

    LuaEngine& lua_;
    std::vector<PluginInfo> plugins_;

    // Lazy loading triggers
    std::unordered_map<std::string, std::vector<std::string>> lazy_event_triggers_;   // event -> plugin names
    std::unordered_map<std::string, std::vector<std::string>> lazy_command_triggers_;  // command -> plugin names
};

} // namespace termcore
