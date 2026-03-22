#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace termcore {

/// A tmux window in control mode
struct TmuxWindow {
    int id = -1;
    std::string name;
    int width = 0;
    int height = 0;
    bool active = false;
};

/// A tmux pane in control mode
struct TmuxPane {
    int id = -1;
    int windowId = -1;
    int width = 0;
    int height = 0;
    int x = 0;  // position within window
    int y = 0;
    bool active = false;
    std::string title;
};

/// tmux control mode notification types
enum class TmuxNotification {
    WindowAdd,
    WindowClose,
    WindowRenamed,
    PaneOutput,
    SessionChanged,
    SessionsChanged,
    LayoutChanged,
    ClientDetached,
    Exit,
    Unknown,
};

/// Parsed tmux control mode message
struct TmuxMessage {
    TmuxNotification type = TmuxNotification::Unknown;
    std::string raw;       // raw line from tmux
    int windowId = -1;
    int paneId = -1;
    std::string data;      // additional data
};

/// Callbacks for tmux events
struct TmuxCallbacks {
    std::function<void(const TmuxWindow&)> onWindowAdded;
    std::function<void(int windowId)> onWindowClosed;
    std::function<void(int windowId, const std::string& name)> onWindowRenamed;
    std::function<void(int paneId, const std::string& output)> onPaneOutput;
    std::function<void(const std::string& layout)> onLayoutChanged;
    std::function<void()> onDetached;
};

/// tmux control mode parser and state manager
class TmuxIntegration {
public:
    TmuxIntegration();

    /// Set callbacks
    void setCallbacks(const TmuxCallbacks& callbacks);

    /// Feed raw data from tmux -CC output
    /// Parses line by line, fires callbacks
    void feedData(const std::string& data);

    /// Parse a single control mode line
    TmuxMessage parseLine(const std::string& line) const;

    /// Send command to tmux (returns the command string to write to PTY)
    std::string sendCommand(const std::string& cmd) const;

    /// Convenience commands
    std::string newWindow(const std::string& name = "") const;
    std::string closeWindow(int windowId) const;
    std::string selectWindow(int windowId) const;
    std::string splitPane(int paneId, bool horizontal) const;
    std::string closePane(int paneId) const;
    std::string selectPane(int paneId) const;
    std::string renameWindow(int windowId, const std::string& name) const;
    std::string listWindows() const;
    std::string listPanes(int windowId) const;
    std::string sendKeys(int paneId, const std::string& keys) const;
    std::string resizePane(int paneId, int width, int height) const;

    /// State
    const std::map<int, TmuxWindow>& windows() const;
    const std::map<int, TmuxPane>& panes() const;
    bool isActive() const;
    void setActive(bool active);

    /// Initial handshake detection
    /// Returns true if the line looks like tmux control mode start
    static bool isControlModeStart(const std::string& line);

private:
    bool active_ = false;
    std::string lineBuffer_;
    TmuxCallbacks callbacks_;
    std::map<int, TmuxWindow> windows_;
    std::map<int, TmuxPane> panes_;

    void processLine(const std::string& line);
    void handleNotification(const TmuxMessage& msg);
};

}  // namespace termcore
