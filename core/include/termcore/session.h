#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace termcore {

/// Window geometry for session save/restore.
struct WindowGeometry {
    int x = 0;
    int y = 0;
    int width = 800;
    int height = 600;
};

/// Per-pane session data.
struct PaneSessionData {
    uint32_t serial = 0;             // Unique serial within session
    std::string working_dir;
    std::string title;
    int rows = 24;
    int cols = 80;
    std::vector<std::string> scrollback_lines;  // Raw lines (before compression)
    bool is_webview = false;
    std::string webview_url;
};

/// Recursive split tree for serialization.
struct SplitNodeData {
    bool is_leaf = true;
    uint32_t leaf_serial = 0;        // References PaneSessionData::serial
    int direction = 0;               // 0=Horizontal, 1=Vertical
    float ratio = 0.5f;
    std::unique_ptr<SplitNodeData> first;
    std::unique_ptr<SplitNodeData> second;
};

/// Per-tab session data.
struct TabSessionData {
    std::string title;
    std::unique_ptr<SplitNodeData> root;
    uint32_t active_pane_serial = 0;
};

/// Per-workspace session data.
struct WorkspaceSessionData {
    std::string name;
    std::vector<TabSessionData> tabs;
    size_t active_tab_index = 0;
};

/// Top-level session data.
struct SessionData {
    int version = 1;
    WindowGeometry window;
    std::vector<WorkspaceSessionData> workspaces;
    size_t active_workspace_index = 0;
    std::vector<PaneSessionData> panes;
};

/// Interface for querying pane state during capture.
class IPaneStateProvider {
public:
    virtual ~IPaneStateProvider() = default;

    virtual std::string getWorkingDir(uint32_t pane_id) const = 0;
    virtual std::string getTitle(uint32_t pane_id) const = 0;
    virtual int getRows(uint32_t pane_id) const = 0;
    virtual int getCols(uint32_t pane_id) const = 0;
    virtual size_t getScrollbackSize(uint32_t pane_id) const = 0;
    virtual std::string getScrollbackLine(uint32_t pane_id, size_t line) const = 0;
    virtual bool isWebView(uint32_t pane_id) const = 0;
    virtual std::string getWebViewUrl(uint32_t pane_id) const = 0;
};

class Mux;  // Forward declaration

/// Session manager: capture, save, load, query.
class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    /// Capture current state from Mux + pane provider into SessionData.
    SessionData capture(const Mux& mux,
                        const IPaneStateProvider& provider,
                        const WindowGeometry& window = {});

    /// Save session to disk (atomic write).
    bool save(const SessionData& data, const std::string& dir = "");

    /// Load session from disk.
    std::optional<SessionData> load(const std::string& dir = "");

    /// Check if a saved session exists.
    bool hasSavedSession(const std::string& dir = "");

    /// Default session directory for this platform.
    static std::string defaultSessionDir();

    /// Full path to session file within given directory.
    static std::string sessionFilePath(const std::string& dir);
};

}  // namespace termcore
