#include "termcore/tmux_integration.h"

#include <cstdlib>
#include <sstream>

namespace termcore {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TmuxIntegration::TmuxIntegration() = default;

void TmuxIntegration::setCallbacks(const TmuxCallbacks& callbacks) {
    callbacks_ = callbacks;
}

// ---------------------------------------------------------------------------
// Data ingestion
// ---------------------------------------------------------------------------

void TmuxIntegration::feedData(const std::string& data) {
    lineBuffer_ += data;

    // Process all complete lines (delimited by '\n')
    size_t pos = 0;
    while (true) {
        size_t nl = lineBuffer_.find('\n', pos);
        if (nl == std::string::npos) break;

        std::string line = lineBuffer_.substr(pos, nl - pos);
        // Strip trailing '\r' if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        processLine(line);
        pos = nl + 1;
    }

    // Keep the remaining incomplete line in the buffer
    if (pos > 0) {
        lineBuffer_ = lineBuffer_.substr(pos);
    }
}

// ---------------------------------------------------------------------------
// Line parsing
// ---------------------------------------------------------------------------

TmuxMessage TmuxIntegration::parseLine(const std::string& line) const {
    TmuxMessage msg;
    msg.raw = line;

    if (line.empty() || line[0] != '%') {
        return msg;
    }

    // %window-add @<id>
    if (line.rfind("%window-add ", 0) == 0) {
        msg.type = TmuxNotification::WindowAdd;
        // Parse @<id>
        auto atPos = line.find('@');
        if (atPos != std::string::npos) {
            msg.windowId = std::atoi(line.c_str() + atPos + 1);
        }
        return msg;
    }

    // %window-close @<id>
    if (line.rfind("%window-close ", 0) == 0) {
        msg.type = TmuxNotification::WindowClose;
        auto atPos = line.find('@');
        if (atPos != std::string::npos) {
            msg.windowId = std::atoi(line.c_str() + atPos + 1);
        }
        return msg;
    }

    // %window-renamed @<id> <name>
    if (line.rfind("%window-renamed ", 0) == 0) {
        msg.type = TmuxNotification::WindowRenamed;
        auto atPos = line.find('@');
        if (atPos != std::string::npos) {
            msg.windowId = std::atoi(line.c_str() + atPos + 1);
            // Find the space after the window id to get the name
            auto spaceAfterId = line.find(' ', atPos);
            if (spaceAfterId != std::string::npos) {
                msg.data = line.substr(spaceAfterId + 1);
            }
        }
        return msg;
    }

    // %output %<paneId> <data>
    if (line.rfind("%output ", 0) == 0) {
        msg.type = TmuxNotification::PaneOutput;
        // Find %<paneId>
        auto pctPos = line.find('%', 1);  // skip the leading %
        if (pctPos != std::string::npos) {
            msg.paneId = std::atoi(line.c_str() + pctPos + 1);
            // Find the space after the pane id to get the data
            auto spaceAfterPane = line.find(' ', pctPos);
            if (spaceAfterPane != std::string::npos) {
                msg.data = line.substr(spaceAfterPane + 1);
            }
        }
        return msg;
    }

    // %layout-change @<windowId> <layout>
    if (line.rfind("%layout-change ", 0) == 0) {
        msg.type = TmuxNotification::LayoutChanged;
        auto atPos = line.find('@');
        if (atPos != std::string::npos) {
            msg.windowId = std::atoi(line.c_str() + atPos + 1);
            auto spaceAfterId = line.find(' ', atPos);
            if (spaceAfterId != std::string::npos) {
                msg.data = line.substr(spaceAfterId + 1);
            }
        }
        return msg;
    }

    // %session-changed $<id> <name>
    if (line.rfind("%session-changed ", 0) == 0) {
        msg.type = TmuxNotification::SessionChanged;
        msg.data = line.substr(17);  // len("%session-changed ")
        return msg;
    }

    // %sessions-changed
    if (line.rfind("%sessions-changed", 0) == 0) {
        msg.type = TmuxNotification::SessionsChanged;
        return msg;
    }

    // %client-detached
    if (line.rfind("%client-detached", 0) == 0) {
        msg.type = TmuxNotification::ClientDetached;
        return msg;
    }

    // %exit or %exit <reason>
    if (line.rfind("%exit", 0) == 0) {
        msg.type = TmuxNotification::Exit;
        if (line.size() > 6) {
            msg.data = line.substr(6);
        }
        return msg;
    }

    // %begin, %end, %error - command response delimiters
    // We parse them but classify as Unknown since they are not
    // standalone notifications; callers can inspect raw for these.
    return msg;
}

// ---------------------------------------------------------------------------
// Command generation
// ---------------------------------------------------------------------------

std::string TmuxIntegration::sendCommand(const std::string& cmd) const {
    return cmd + "\n";
}

std::string TmuxIntegration::newWindow(const std::string& name) const {
    if (name.empty()) {
        return sendCommand("new-window");
    }
    return sendCommand("new-window -n \"" + name + "\"");
}

std::string TmuxIntegration::closeWindow(int windowId) const {
    return sendCommand("kill-window -t @" + std::to_string(windowId));
}

std::string TmuxIntegration::selectWindow(int windowId) const {
    return sendCommand("select-window -t @" + std::to_string(windowId));
}

std::string TmuxIntegration::splitPane(int paneId, bool horizontal) const {
    std::string flag = horizontal ? "-h" : "-v";
    return sendCommand("split-window " + flag + " -t %" + std::to_string(paneId));
}

std::string TmuxIntegration::closePane(int paneId) const {
    return sendCommand("kill-pane -t %" + std::to_string(paneId));
}

std::string TmuxIntegration::selectPane(int paneId) const {
    return sendCommand("select-pane -t %" + std::to_string(paneId));
}

std::string TmuxIntegration::renameWindow(int windowId, const std::string& name) const {
    return sendCommand("rename-window -t @" + std::to_string(windowId) + " \"" + name + "\"");
}

std::string TmuxIntegration::listWindows() const {
    return sendCommand("list-windows");
}

std::string TmuxIntegration::listPanes(int windowId) const {
    return sendCommand("list-panes -t @" + std::to_string(windowId));
}

std::string TmuxIntegration::sendKeys(int paneId, const std::string& keys) const {
    return sendCommand("send-keys -t %" + std::to_string(paneId) + " " + keys);
}

std::string TmuxIntegration::resizePane(int paneId, int width, int height) const {
    return sendCommand("resize-pane -t %" + std::to_string(paneId) +
                       " -x " + std::to_string(width) +
                       " -y " + std::to_string(height));
}

// ---------------------------------------------------------------------------
// State accessors
// ---------------------------------------------------------------------------

const std::map<int, TmuxWindow>& TmuxIntegration::windows() const {
    return windows_;
}

const std::map<int, TmuxPane>& TmuxIntegration::panes() const {
    return panes_;
}

bool TmuxIntegration::isActive() const {
    return active_;
}

void TmuxIntegration::setActive(bool active) {
    active_ = active;
}

// ---------------------------------------------------------------------------
// Handshake detection
// ---------------------------------------------------------------------------

bool TmuxIntegration::isControlModeStart(const std::string& line) {
    // tmux control mode begins with %begin or DCS sequence \033P1000p
    if (line.rfind("%begin", 0) == 0) {
        return true;
    }
    if (line.find("\033P1000p") != std::string::npos) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Internal processing
// ---------------------------------------------------------------------------

void TmuxIntegration::processLine(const std::string& line) {
    if (line.empty()) return;

    TmuxMessage msg = parseLine(line);
    if (msg.type != TmuxNotification::Unknown) {
        handleNotification(msg);
    }
}

void TmuxIntegration::handleNotification(const TmuxMessage& msg) {
    switch (msg.type) {
    case TmuxNotification::WindowAdd: {
        TmuxWindow win;
        win.id = msg.windowId;
        windows_[msg.windowId] = win;
        if (callbacks_.onWindowAdded) {
            callbacks_.onWindowAdded(win);
        }
        break;
    }
    case TmuxNotification::WindowClose: {
        windows_.erase(msg.windowId);
        if (callbacks_.onWindowClosed) {
            callbacks_.onWindowClosed(msg.windowId);
        }
        break;
    }
    case TmuxNotification::WindowRenamed: {
        auto it = windows_.find(msg.windowId);
        if (it != windows_.end()) {
            it->second.name = msg.data;
        }
        if (callbacks_.onWindowRenamed) {
            callbacks_.onWindowRenamed(msg.windowId, msg.data);
        }
        break;
    }
    case TmuxNotification::PaneOutput: {
        if (callbacks_.onPaneOutput) {
            callbacks_.onPaneOutput(msg.paneId, msg.data);
        }
        break;
    }
    case TmuxNotification::LayoutChanged: {
        if (callbacks_.onLayoutChanged) {
            callbacks_.onLayoutChanged(msg.data);
        }
        break;
    }
    case TmuxNotification::ClientDetached: {
        active_ = false;
        if (callbacks_.onDetached) {
            callbacks_.onDetached();
        }
        break;
    }
    case TmuxNotification::Exit: {
        active_ = false;
        if (callbacks_.onDetached) {
            callbacks_.onDetached();
        }
        break;
    }
    default:
        break;
    }
}

}  // namespace termcore
