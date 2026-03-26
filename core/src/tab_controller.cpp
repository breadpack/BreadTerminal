#include "termcore/tab_controller.h"
#include "lua_bindings/lua_tab_module.h"  // for TabTitleInfo
#include <algorithm>

namespace termcore {

// Build a display title for a tab.
// Title formatting is now handled by Lua (defaults/tab_title.lua).
// This fallback only runs if Lua callback is not set.
static std::string buildTabTitle(const std::string& title, const std::string& cwd,
                                  const std::string& processName = {}) {
    if (!title.empty()) return title;
    if (!cwd.empty()) return cwd;
    if (!processName.empty()) return processName;
    return "Terminal";
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

        // Try Lua title format callback (can override built title)
        if (titleFormatFn_ && !inTitleFormat_) {
            TabTitleInfo luaInfo;
            luaInfo.tab_index = static_cast<int>(i);
            luaInfo.process_name = processName;
            luaInfo.working_dir = cwd;
            luaInfo.title = screenTitle;
            luaInfo.is_active = ti.active;
            inTitleFormat_ = true;
            auto custom = titleFormatFn_(luaInfo);
            inTitleFormat_ = false;
            if (!custom.empty()) {
                ti.title = std::move(custom);
            }
        }

        // Pass through icon name and process name for renderer
        ti.icon_name = iconName;
        ti.process_name = processName;

        // Agent state for tab bar rendering
        if (agent_tracker_) {
            auto* agent = agent_tracker_->getAgent(activePane);
            if (agent) {
                ti.agent_state = agent->state;
            }
        }

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

    Screen* screenPtr = ps->screen.get();
    panes_[id] = std::move(ps);
    if (onPaneCreated_) onPaneCreated_(screenPtr);
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

        // Read only one chunk per call so the main loop can process
        // keyboard input between reads (avoids input lag during heavy output).
        int n = ps->pty->read(buf, sizeof(buf));
        if (n > 0) {
            ps->parser->feed(buf, static_cast<size_t>(n));
            dataRead = true;
        }

        if (dataRead && wasAtBottom && ps->screen) {
            ps->screen->scrollViewportToBottom();
        }
    }
    return dataRead;
}

std::vector<void*> TabController::collectReadHandles() const {
    std::vector<void*> handles;
    for (const auto& [id, ps] : panes_) {
        if (!ps->pty || !ps->pty->isAlive()) continue;
        void* h = ps->pty->nativeReadHandle();
        if (h) handles.push_back(h);
    }
    return handles;
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

void TabController::setCustomTitle(int tab_index, const std::string& title) {
    customTitles_[tab_index] = title;
}

std::string TabController::customTitle(int tab_index) const {
    auto it = customTitles_.find(tab_index);
    return (it != customTitles_.end()) ? it->second : std::string{};
}

void TabController::setProcessIcon(const std::string& process, const std::string& icon) {
    processIcons_[process] = icon;
}

std::string TabController::getProcessIcon(const std::string& process) const {
    auto it = processIcons_.find(process);
    return it != processIcons_.end() ? it->second : "";
}

} // namespace termcore
