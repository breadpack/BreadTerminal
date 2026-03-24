#pragma once

#include <string>
#include <vector>

namespace termcore {

// Plugin capabilities that can be requested
enum class PluginCapability {
    Events,        // Listen to terminal events (bell, title change, etc.)
    Keybindings,   // Register custom keybindings
    Config,        // Read/modify terminal configuration
    Notifications, // Send notifications
    PaneRead,      // Read pane content
    PaneWrite,     // Write to pane (send text)
    FileSystem,    // Access file system (restricted)
    UI,          // Extend settings UI
    Clipboard,   // Access clipboard history and paste guard
};

struct PluginMetadata {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::vector<PluginCapability> capabilities;
    std::string entry_file;  // Lua file path
};

// Plugin state tracking
enum class PluginState {
    Discovered,  // Found on disk, metadata parsed
    Loaded,      // Lua code executed
    Active,      // Events connected
    Error,       // Failed to load/execute
    Disabled,    // Manually disabled by user
};

struct PluginInfo {
    PluginMetadata metadata;
    PluginState state = PluginState::Discovered;
    std::string error_message;
    std::string directory;  // Plugin root directory
};

} // namespace termcore
