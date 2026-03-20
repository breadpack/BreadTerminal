#include "termcore/tab_controller.h"
#include <algorithm>

namespace termcore {

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

void TabController::createTab(int rows, int cols) {
    mux_->createTab(wsId_, rows, cols);
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

void TabController::splitRight(int rows, int cols) {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return;
    mux_->splitPane(wsId_, tab->id, tab->active_pane,
                    SplitDirection::Horizontal, rows, cols);
    syncActivePointers();
}

void TabController::splitDown(int rows, int cols) {
    auto* tab = mux_->activeTab(wsId_);
    if (!tab) return;
    mux_->splitPane(wsId_, tab->id, tab->active_pane,
                    SplitDirection::Vertical, rows, cols);
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
        ti.title = ws->tabs[i]->title.empty()
            ? "Tab " + std::to_string(i + 1)
            : ws->tabs[i]->title;
        ti.active = (i == ws->active_tab_index);
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

    if (ptyFactory_) {
        ps->pty = ptyFactory_(rows, cols);
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
