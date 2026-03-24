#include "termcore/tab_controller.h"
#include <algorithm>

namespace termcore {

// Check if a title looks like a bare shell/process name
static bool isShellName(const std::string& s) {
    // Strip path to get basename
    auto slash = s.find_last_of("/\\");
    std::string base = (slash != std::string::npos) ? s.substr(slash + 1) : s;

    // Common shell executables
    static const char* shells[] = {
        "cmd.exe", "cmd", "powershell.exe", "powershell",
        "pwsh.exe", "pwsh", "bash", "bash.exe",
        "zsh", "fish", "sh", "wsl.exe", "wsl",
        "Command Prompt", "Windows PowerShell",
    };
    for (auto* sh : shells) {
        if (base == sh) return true;
    }
    return false;
}

// Extract last path component from a path string
static std::string lastPathComponent(const std::string& path) {
    if (path.empty()) return {};
    // Trim trailing slashes
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) --end;
    if (end == 0) return "/";
    auto slash = path.find_last_of("/\\", end - 1);
    if (slash == std::string::npos) return path.substr(0, end);
    return path.substr(slash + 1, end - slash - 1);
}

// Build a display title for a tab.
// Priority: meaningful screen title > process name > working directory > shell name
// Examples:
//   title="vim main.cpp" → "vim main.cpp"
//   title="cmd.exe", proc="git", cwd="" → "git"
//   title="cmd.exe", proc="cmd", cwd="C:\Projects\Foo" → "Foo"
//   title="MINGW64:/c/Users/.../Foo" → "MINGW64:Foo"
//   title="", proc="python", cwd="" → "python"
static std::string buildTabTitle(const std::string& title, const std::string& cwd,
                                  const std::string& processName = {}) {
    if (title.empty() && cwd.empty() && processName.empty()) return {};

    // If the screen title is a shell name, look for better info
    if (title.empty() || isShellName(title)) {
        // If foreground process differs from shell, show it (e.g. "git", "python", "vim")
        if (!processName.empty() && !isShellName(processName)) {
            return processName;
        }
        // Otherwise show working directory
        if (!cwd.empty()) {
            std::string dir = lastPathComponent(cwd);
            if (!dir.empty()) return dir;
        }
        // Fall back to process name even if it's a shell
        if (!processName.empty()) return processName;
        if (!title.empty()) return title;
        return {};
    }

    // Handle "PREFIX:path" patterns (e.g. "MINGW64:/c/Users/.../Foo")
    auto colon = title.find(':');
    if (colon != std::string::npos && colon < 20) {
        std::string after = title.substr(colon + 1);
        size_t start = 0;
        while (start < after.size() && after[start] == ' ') ++start;
        after = after.substr(start);
        bool looksLikePath = false;
        if (!after.empty() && (after[0] == '/' || after[0] == '~'))
            looksLikePath = true;
        if (after.size() >= 2 && std::isalpha(after[0]) && after[1] == ':')
            looksLikePath = true;
        if (after.size() >= 3 && after[0] == '/' && std::isalpha(after[1]) && after[2] == '/')
            looksLikePath = true;

        if (looksLikePath) {
            std::string prefix = title.substr(0, colon + 1);
            std::string dir = lastPathComponent(after);
            return dir.empty() ? title : prefix + dir;
        }
    }

    // If title itself is a long path, shorten it
    if (title.find_first_of("/\\") != std::string::npos) {
        std::string dir = lastPathComponent(title);
        if (!dir.empty()) return dir;
    }

    return title;
}

TabController::TabController(std::unique_ptr<Mux> mux, WorkspaceId wsId,
                             PtyFactory ptyFactory, const Config& config)
    : mux_(std::move(mux))
    , wsId_(wsId)
    , ptyFactory_(std::move(ptyFactory))
    , config_(config)
{
    // Set up Mux callbacks so it delegates pane creation/destruction to us
    mux_->setPaneCallbacks(
        [this](int rows, int cols) { return createPaneState(rows, cols); },
        [this](PaneId id) { destroyPaneState(id); }
    );
}

// --- Tab operations ---

void TabController::createTab(int rows, int cols, const std::string& profile_id) {
    pendingProfileId_ = profile_id;
    mux_->createTab(wsId_, rows, cols);
    pendingProfileId_.clear();
    syncActivePointers();
}

void TabController::closeTab() {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return;
    mux_->destroyTab(wsId_, tab->id);
    syncActivePointers();
}

void TabController::nextTab() {
    auto* ws = mux_->getWorkspace(wsId_);
    if (!ws || ws->tabs.size() <= 1) return;
    size_t next = (ws->active_tab_index + 1) % ws->tabs.size();
    mux_->setActiveTab(wsId_, ws->tabs[next]->id);
    syncActivePointers();
}

void TabController::prevTab() {
    auto* ws = mux_->getWorkspace(wsId_);
    if (!ws || ws->tabs.size() <= 1) return;
    size_t prev = (ws->active_tab_index + ws->tabs.size() - 1) % ws->tabs.size();
    mux_->setActiveTab(wsId_, ws->tabs[prev]->id);
    syncActivePointers();
}

void TabController::switchToTab(int index) {
    auto* ws = mux_->getWorkspace(wsId_);
    if (!ws) return;
    if (index < 0 || index >= static_cast<int>(ws->tabs.size())) return;
    mux_->setActiveTab(wsId_, ws->tabs[index]->id);
    syncActivePointers();
}

// --- Pane operations ---

void TabController::splitRight(int rows, int cols, const std::string& profile_id) {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return;
    pendingProfileId_ = profile_id;
    mux_->splitPane(wsId_, tab->id, tab->active_pane,
                    SplitDirection::Horizontal, rows, cols);
    pendingProfileId_.clear();
    syncActivePointers();
}

void TabController::splitDown(int rows, int cols, const std::string& profile_id) {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return;
    pendingProfileId_ = profile_id;
    mux_->splitPane(wsId_, tab->id, tab->active_pane,
                    SplitDirection::Vertical, rows, cols);
    pendingProfileId_.clear();
    syncActivePointers();
}

void TabController::closePane() {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return;
    mux_->closePane(wsId_, tab->id, tab->active_pane);
    syncActivePointers();
}

// --- Active pane access ---

Screen* TabController::activeScreen() {
    return activeScreen_;
}

const Screen* TabController::activeScreen() const {
    return activeScreen_;
}

Pty* TabController::activePty() {
    return activePty_;
}

PaneState* TabController::activePane() {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return nullptr;
    auto it = panes_.find(tab->active_pane);
    return (it != panes_.end()) ? it->second.get() : nullptr;
}

PaneState* TabController::paneById(PaneId id) {
    auto it = panes_.find(id);
    return (it != panes_.end()) ? it->second.get() : nullptr;
}

// --- Tab bar info ---

std::vector<TabController::TabInfo> TabController::tabBarInfo() const {
    std::vector<TabInfo> info;
    auto* ws = mux_->getWorkspace(wsId_);
    if (!ws) return info;

    for (size_t i = 0; i < ws->tabs.size(); ++i) {
        TabInfo ti;
        ti.active = (i == ws->active_tab_index);

        // Gather info from active pane's screen and PTY
        std::string screenTitle;
        std::string iconName;
        std::string cwd;
        std::string processName;
        PaneId activePane = ws->tabs[i]->active_pane;
        auto it = panes_.find(activePane);
        if (it != panes_.end()) {
            if (it->second->screen) {
                screenTitle = it->second->screen->title();
                iconName = it->second->screen->iconName();
                cwd = it->second->screen->workingDirectory();
            }
            if (it->second->pty) {
                processName = it->second->pty->foregroundProcessName();
            }
        }

        // Build display title
        std::string built = buildTabTitle(screenTitle, cwd, processName);
        if (!built.empty()) {
            ti.title = built;
        } else if (!ws->tabs[i]->title.empty()) {
            ti.title = ws->tabs[i]->title;
        } else {
            ti.title = "Tab " + std::to_string(i + 1);
        }

        // Pass through icon name and process name for renderer
        ti.icon_name = iconName;
        ti.process_name = processName;

        info.push_back(std::move(ti));
    }
    return info;
}

int TabController::tabCount() const {
    auto* ws = mux_->getWorkspace(wsId_);
    return ws ? static_cast<int>(ws->tabs.size()) : 0;
}

// --- Pane lifecycle ---

PaneId TabController::createPaneState(int rows, int cols) {
    PaneId id = nextPaneId_++;
    auto ps = std::make_unique<PaneState>();
    ps->id = id;
    ps->screen = std::make_unique<Screen>();
    ps->screen->resize(rows, cols);
    ps->parser = std::make_unique<VtParser>(*ps->screen);

    // Resolve profile from pendingProfileId_
    Profile profile;
    if (profileMgr_) {
        if (!pendingProfileId_.empty()) {
            auto* p = profileMgr_->findProfile(pendingProfileId_);
            profile = p ? *p : profileMgr_->defaultProfile();
        } else {
            profile = profileMgr_->defaultProfile();
        }
    }
    // When profileMgr_ is not set (e.g., C API path, tests), profile
    // remains default-constructed — PtyFactory will use its own defaults.
    ps->profile_id = profile.id;

    if (ptyFactory_) {
        ps->pty = ptyFactory_(profile, rows, cols);
    }

    panes_[id] = std::move(ps);
    return id;
}

void TabController::destroyPaneState(PaneId id) {
    panes_.erase(id);
}

bool TabController::hasAnyAlivePty() const {
    for (auto& [id, ps] : panes_) {
        if (ps->pty && ps->pty->isAlive()) return true;
    }
    return false;
}

void TabController::syncActivePointers() {
    auto* ps = activePane();
    activeScreen_ = ps ? ps->screen.get() : nullptr;
    activePty_ = ps ? ps->pty.get() : nullptr;
}

// --- Poll & resize ---

bool TabController::pollAllPtys() {
    bool dataRead = false;
    char buf[8192];

    for (auto& [id, ps] : panes_) {
        if (!ps->pty) continue;

        bool wasAtBottom = ps->screen ? ps->screen->isViewportAtBottom() : true;

        int n = ps->pty->read(buf, sizeof(buf));
        while (n > 0) {
            ps->parser->feed(buf, static_cast<size_t>(n));
            dataRead = true;
            n = ps->pty->read(buf, sizeof(buf));
        }

        if (dataRead && wasAtBottom && ps->screen) {
            ps->screen->scrollViewportToBottom();
        }
    }
    return dataRead;
}

bool TabController::cleanupDeadPanes() {
    std::vector<PaneId> deadPanes;
    for (auto& [id, ps] : panes_) {
        if (ps->pty && !ps->pty->isAlive()) {
            deadPanes.push_back(id);
        }
    }

    for (PaneId deadId : deadPanes) {
        auto tabIds = mux_->allTabIds(wsId_);
        for (auto tid : tabIds) {
            auto allPanesInTab = mux_->allPanes(wsId_, tid);
            for (auto pid : allPanesInTab) {
                if (pid == deadId) {
                    if (allPanesInTab.size() == 1 && tabIds.size() == 1) {
                        return true; // signal: close window
                    }
                    mux_->closePane(wsId_, tid, deadId);
                    syncActivePointers();
                    goto nextDead;
                }
            }
        }
        nextDead:;
    }
    return false;
}

void TabController::resizeAllPanes(int rows, int cols) {
    for (auto& [id, ps] : panes_) {
        if (ps->screen) ps->screen->resize(rows, cols);
        if (ps->pty && ps->pty->isAlive()) ps->pty->resize(rows, cols);
    }
}

} // namespace termcore
