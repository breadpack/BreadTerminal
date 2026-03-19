#ifndef TERMCORE_MCP_SERVER_SPEC_H
#define TERMCORE_MCP_SERVER_SPEC_H

#include <string>
#include <vector>

namespace termcore {

/// Specification for a single MCP tool exposed by BreadTerminal.
struct McpToolSpec {
    std::string name;
    std::string description;
    std::string input_schema_json;  // JSON Schema string
};

/// Provides Model Context Protocol tool specifications.
/// These define the capabilities that BreadTerminal exposes to
/// MCP-aware agents (e.g., Claude Code).
class McpServerSpec {
public:
    /// Return all tool specifications.
    static std::vector<McpToolSpec> allTools();

    /// Workspace management tools.
    static std::vector<McpToolSpec> workspaceTools();

    /// Pane manipulation and I/O tools.
    static std::vector<McpToolSpec> paneTools();

    /// Agent orchestration tools.
    static std::vector<McpToolSpec> agentTools();

    /// Browser automation tools.
    static std::vector<McpToolSpec> browserTools();

    /// Status and progress tools.
    static std::vector<McpToolSpec> statusTools();

    /// Notification tools.
    static std::vector<McpToolSpec> notificationTools();
};

} // namespace termcore

#endif // TERMCORE_MCP_SERVER_SPEC_H
