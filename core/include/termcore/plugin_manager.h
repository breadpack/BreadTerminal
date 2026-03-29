#pragma once

#include "termcore/lua_engine.h"
#include "termcore/plugin.h"
#include "termcore/result.h"

#include <string>
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

private:
    // Parse plugin.lua metadata file (directory-based plugins).
    Result<PluginMetadata> parseMetadata(const std::string& plugin_dir);

    // Parse metadata from a single .lua file plugin.
    Result<PluginMetadata> parseSingleFileMetadata(const std::string& lua_file);

    // Apply sandbox restrictions based on granted capabilities.
    void applySandbox(const PluginMetadata& meta);

    // Restore globals that were nil'd by applySandbox.
    void restoreSandbox(const PluginMetadata& meta);

    LuaEngine& lua_;
    std::vector<PluginInfo> plugins_;
};

} // namespace termcore
