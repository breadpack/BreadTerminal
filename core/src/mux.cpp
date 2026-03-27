#include "termcore/mux.h"
#include "termcore/ssh_mux.h"

#include <algorithm>
#include <cmath>

namespace termcore {

Mux::Mux() = default;
Mux::~Mux() = default;

void Mux::setPaneCallbacks(PaneCreateCallback create_cb, PaneDestroyCallback destroy_cb) {
    create_cb_ = std::move(create_cb);
    destroy_cb_ = std::move(destroy_cb);
}

void Mux::setOnChanged(MuxChangeCallback cb) {
    on_changed_ = std::move(cb);
}

void Mux::fireOnChanged() {
    if (on_changed_) on_changed_();
}

// --- Workspace ---

WorkspaceId Mux::createWorkspace(const std::string& name) {
    auto ws = std::make_unique<Workspace>();
    ws->id = next_workspace_id_++;
    ws->name = name.empty() ? ("Workspace " + std::to_string(ws->id)) : name;
    WorkspaceId id = ws->id;
    workspaces_.push_back(std::move(ws));
    if (active_workspace_ == kInvalidWorkspace) {
        active_workspace_ = id;
    }
    fireOnChanged();
    return id;
}

void Mux::destroyWorkspace(WorkspaceId id) {
    auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                           [id](const auto& ws) { return ws->id == id; });
    if (it == workspaces_.end()) return;

    // Destroy all panes in all tabs
    for (auto& tab : (*it)->tabs) {
        if (tab->root) {
            destroyAllPanes(tab->root.get());
        }
    }

    workspaces_.erase(it);

    if (active_workspace_ == id) {
        active_workspace_ = workspaces_.empty() ? kInvalidWorkspace : workspaces_.front()->id;
    }
    fireOnChanged();
}

Workspace* Mux::getWorkspace(WorkspaceId id) {
    for (auto& ws : workspaces_) {
        if (ws->id == id) return ws.get();
    }
    return nullptr;
}

WorkspaceId Mux::activeWorkspaceId() const {
    return active_workspace_;
}

void Mux::setActiveWorkspace(WorkspaceId id) {
    for (auto& ws : workspaces_) {
        if (ws->id == id) {
            active_workspace_ = id;
            fireOnChanged();
            return;
        }
    }
}

// --- Tab ---

TabId Mux::createTab(WorkspaceId ws_id, int rows, int cols) {
    auto* ws = getWorkspace(ws_id);
    if (!ws) return kInvalidTab;

    auto tab = std::make_unique<Tab>();
    tab->id = next_tab_id_++;
    tab->title = "Tab " + std::to_string(tab->id);

    auto root = std::make_unique<SplitNode>();
    root->is_leaf = true;
    if (create_cb_) {
        root->pane_id = create_cb_(rows, cols);
    }
    tab->active_pane = root->pane_id;
    tab->root = std::move(root);

    TabId id = tab->id;
    ws->tabs.push_back(std::move(tab));
    ws->active_tab_index = ws->tabs.size() - 1;
    fireOnChanged();
    return id;
}

void Mux::destroyTab(WorkspaceId ws_id, TabId tab_id) {
    auto* ws = getWorkspace(ws_id);
    if (!ws) return;

    auto it = std::find_if(ws->tabs.begin(), ws->tabs.end(),
                           [tab_id](const auto& t) { return t->id == tab_id; });
    if (it == ws->tabs.end()) return;

    if ((*it)->root) {
        destroyAllPanes((*it)->root.get());
    }

    size_t idx = static_cast<size_t>(std::distance(ws->tabs.begin(), it));
    ws->tabs.erase(it);

    if (ws->tabs.empty()) {
        ws->active_tab_index = 0;
    } else if (ws->active_tab_index >= ws->tabs.size()) {
        ws->active_tab_index = ws->tabs.size() - 1;
    } else if (idx < ws->active_tab_index) {
        ws->active_tab_index--;
    }
    fireOnChanged();
}

Tab* Mux::activeTab(WorkspaceId ws_id) {
    auto* ws = getWorkspace(ws_id);
    if (!ws || ws->tabs.empty()) return nullptr;
    if (ws->active_tab_index >= ws->tabs.size()) return nullptr;
    return ws->tabs[ws->active_tab_index].get();
}

void Mux::setActiveTab(WorkspaceId ws_id, TabId tab_id) {
    auto* ws = getWorkspace(ws_id);
    if (!ws) return;
    for (size_t i = 0; i < ws->tabs.size(); ++i) {
        if (ws->tabs[i]->id == tab_id) {
            ws->active_tab_index = i;
            fireOnChanged();
            return;
        }
    }
}

void Mux::moveTab(WorkspaceId ws_id, TabId tab_id, int newIndex) {
    auto* ws = getWorkspace(ws_id);
    if (!ws || ws->tabs.size() <= 1) return;

    // Find current position
    int oldIndex = -1;
    for (size_t i = 0; i < ws->tabs.size(); ++i) {
        if (ws->tabs[i]->id == tab_id) {
            oldIndex = static_cast<int>(i);
            break;
        }
    }
    if (oldIndex < 0) return;

    // Clamp newIndex
    int maxIndex = static_cast<int>(ws->tabs.size()) - 1;
    if (newIndex < 0) newIndex = 0;
    if (newIndex > maxIndex) newIndex = maxIndex;
    if (newIndex == oldIndex) return;

    // Move the tab
    auto tab = std::move(ws->tabs[oldIndex]);
    ws->tabs.erase(ws->tabs.begin() + oldIndex);
    ws->tabs.insert(ws->tabs.begin() + newIndex, std::move(tab));

    // Update active_tab_index to follow the active tab
    if (ws->active_tab_index == static_cast<size_t>(oldIndex)) {
        ws->active_tab_index = static_cast<size_t>(newIndex);
    } else if (oldIndex < newIndex) {
        if (ws->active_tab_index > static_cast<size_t>(oldIndex) &&
            ws->active_tab_index <= static_cast<size_t>(newIndex)) {
            ws->active_tab_index--;
        }
    } else {
        if (ws->active_tab_index >= static_cast<size_t>(newIndex) &&
            ws->active_tab_index < static_cast<size_t>(oldIndex)) {
            ws->active_tab_index++;
        }
    }

    fireOnChanged();
}

// --- Pane ---

PaneId Mux::splitPane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id,
                       SplitDirection direction, int rows, int cols) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab || !tab->root) return kInvalidPane;

    auto* node = findNode(tab->root.get(), pane_id);
    if (!node || !node->is_leaf) return kInvalidPane;

    PaneId new_pane = kInvalidPane;
    if (create_cb_) {
        new_pane = create_cb_(rows, cols);
    }
    if (new_pane == kInvalidPane) return kInvalidPane;

    // Turn leaf into a split: old pane goes to first, new pane to second
    auto first = std::make_unique<SplitNode>();
    first->is_leaf = true;
    first->pane_id = node->pane_id;

    auto second = std::make_unique<SplitNode>();
    second->is_leaf = true;
    second->pane_id = new_pane;

    node->is_leaf = false;
    node->pane_id = kInvalidPane;
    node->direction = direction;
    node->ratio = 0.5f;
    node->first = std::move(first);
    node->second = std::move(second);

    fireOnChanged();
    return new_pane;
}

void Mux::closePane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab || !tab->root) return;

    // Unzoom if the zoomed pane is being closed
    if (tab->zoomed_pane == pane_id) {
        tab->zoomed_pane = kInvalidPane;
    }

    // If root is the leaf we want to close, destroy the whole tab's tree
    if (tab->root->is_leaf && tab->root->pane_id == pane_id) {
        if (destroy_cb_) destroy_cb_(pane_id);
        tab->root.reset();
        tab->active_pane = kInvalidPane;
        fireOnChanged();
        return;
    }

    // findParent finds the immediate parent split node whose direct child is the leaf
    auto* parent = findParent(tab->root.get(), pane_id);
    if (!parent) return;

    if (destroy_cb_) destroy_cb_(pane_id);

    // Determine which child has the pane and which is the sibling
    std::unique_ptr<SplitNode> sibling;
    if (parent->first && parent->first->is_leaf && parent->first->pane_id == pane_id) {
        sibling = std::move(parent->second);
    } else if (parent->second && parent->second->is_leaf && parent->second->pane_id == pane_id) {
        sibling = std::move(parent->first);
    } else {
        return;  // shouldn't happen given findParent contract
    }

    // Replace parent node in-place with sibling content
    parent->is_leaf = sibling->is_leaf;
    parent->pane_id = sibling->pane_id;
    parent->direction = sibling->direction;
    parent->ratio = sibling->ratio;
    parent->first = std::move(sibling->first);
    parent->second = std::move(sibling->second);

    // Update active pane if needed
    if (tab->active_pane == pane_id) {
        std::vector<PaneId> panes;
        collectPanes(tab->root.get(), panes);
        tab->active_pane = panes.empty() ? kInvalidPane : panes.front();
    }
    fireOnChanged();
}

PaneId Mux::activePaneId(WorkspaceId ws_id, TabId tab_id) const {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab) return kInvalidPane;
    return tab->active_pane;
}

void Mux::setActivePane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab) return;
    // Verify pane exists in the tree
    if (findNode(tab->root.get(), pane_id)) {
        // Unzoom when switching active pane while zoomed
        if (tab->zoomed_pane != kInvalidPane && tab->zoomed_pane != pane_id) {
            tab->zoomed_pane = kInvalidPane;
        }
        tab->active_pane = pane_id;
    }
}

std::vector<PaneId> Mux::allPanes(WorkspaceId ws_id, TabId tab_id) const {
    const auto* tab = findTab(ws_id, tab_id);
    std::vector<PaneId> result;
    if (tab && tab->root) {
        collectPanes(tab->root.get(), result);
    }
    return result;
}

// --- Private helpers ---

SplitNode* Mux::findNode(SplitNode* node, PaneId pane_id) {
    if (!node) return nullptr;
    if (node->is_leaf && node->pane_id == pane_id) return node;
    if (auto* found = findNode(node->first.get(), pane_id)) return found;
    return findNode(node->second.get(), pane_id);
}

SplitNode* Mux::findParent(SplitNode* node, PaneId pane_id) {
    if (!node || node->is_leaf) return nullptr;
    // Check if either child is the target leaf
    if (node->first && node->first->is_leaf && node->first->pane_id == pane_id) return node;
    if (node->second && node->second->is_leaf && node->second->pane_id == pane_id) return node;
    // Recurse
    if (auto* found = findParent(node->first.get(), pane_id)) return found;
    return findParent(node->second.get(), pane_id);
}

void Mux::collectPanes(const SplitNode* node, std::vector<PaneId>& out) const {
    if (!node) return;
    if (node->is_leaf) {
        if (node->pane_id != kInvalidPane) {
            out.push_back(node->pane_id);
        }
        return;
    }
    collectPanes(node->first.get(), out);
    collectPanes(node->second.get(), out);
}

void Mux::destroyAllPanes(const SplitNode* node) {
    if (!node) return;
    if (node->is_leaf) {
        if (destroy_cb_ && node->pane_id != kInvalidPane) {
            destroy_cb_(node->pane_id);
        }
        return;
    }
    destroyAllPanes(node->first.get());
    destroyAllPanes(node->second.get());
}

Tab* Mux::findTab(WorkspaceId ws_id, TabId tab_id) {
    auto* ws = getWorkspace(ws_id);
    if (!ws) return nullptr;
    for (auto& t : ws->tabs) {
        if (t->id == tab_id) return t.get();
    }
    return nullptr;
}

const Tab* Mux::findTab(WorkspaceId ws_id, TabId tab_id) const {
    for (const auto& ws : workspaces_) {
        if (ws->id == ws_id) {
            for (const auto& t : ws->tabs) {
                if (t->id == tab_id) return t.get();
            }
            return nullptr;
        }
    }
    return nullptr;
}

std::vector<WorkspaceId> Mux::allWorkspaceIds() const {
    std::vector<WorkspaceId> ids;
    ids.reserve(workspaces_.size());
    for (const auto& ws : workspaces_) {
        ids.push_back(ws->id);
    }
    return ids;
}

std::vector<TabId> Mux::allTabIds(WorkspaceId ws_id) const {
    std::vector<TabId> ids;
    for (const auto& ws : workspaces_) {
        if (ws->id == ws_id) {
            ids.reserve(ws->tabs.size());
            for (const auto& t : ws->tabs) {
                ids.push_back(t->id);
            }
            break;
        }
    }
    return ids;
}

const SplitNode* Mux::splitRoot(WorkspaceId ws_id, TabId tab_id) const {
    const auto* tab = findTab(ws_id, tab_id);
    return tab ? tab->root.get() : nullptr;
}

// --- Broadcast input ---

BroadcastMode Mux::broadcastMode() const {
    return broadcast_mode_;
}

void Mux::setBroadcastMode(BroadcastMode mode) {
    broadcast_mode_ = mode;
    fireOnChanged();
}

void Mux::toggleBroadcast() {
    switch (broadcast_mode_) {
        case BroadcastMode::Off:      broadcast_mode_ = BroadcastMode::All; break;
        case BroadcastMode::All:      broadcast_mode_ = BroadcastMode::Selected; break;
        case BroadcastMode::Selected: broadcast_mode_ = BroadcastMode::Off; break;
    }
    fireOnChanged();
}

void Mux::addBroadcastTarget(PaneId paneId) {
    if (paneId != kInvalidPane) {
        broadcast_targets_.insert(paneId);
    }
}

void Mux::removeBroadcastTarget(PaneId paneId) {
    broadcast_targets_.erase(paneId);
}

void Mux::clearBroadcastTargets() {
    broadcast_targets_.clear();
}

std::vector<PaneId> Mux::getBroadcastPaneIds() const {
    switch (broadcast_mode_) {
        case BroadcastMode::All: {
            // Return all panes in active workspace's active tab
            const Workspace* ws = nullptr;
            for (const auto& w : workspaces_) {
                if (w->id == active_workspace_) { ws = w.get(); break; }
            }
            if (!ws || ws->tabs.empty()) return {};
            if (ws->active_tab_index >= ws->tabs.size()) return {};
            const auto& tab = ws->tabs[ws->active_tab_index];
            std::vector<PaneId> result;
            collectPanes(tab->root.get(), result);
            return result;
        }
        case BroadcastMode::Selected:
            return {broadcast_targets_.begin(), broadcast_targets_.end()};
        case BroadcastMode::Off:
        default:
            return {};
    }
}

// --- SSH mux integration ---

void Mux::addSshPane(PaneId pane_id, int channel_id,
                      std::shared_ptr<SshMuxSession> session) {
    if (pane_id == kInvalidPane || !session) return;
    SshPaneBinding binding;
    binding.channel_id = channel_id;
    binding.session = std::move(session);
    ssh_panes_[pane_id] = std::move(binding);
}

void Mux::removeSshPane(PaneId pane_id) {
    ssh_panes_.erase(pane_id);
}

bool Mux::isSshPane(PaneId pane_id) const {
    return ssh_panes_.find(pane_id) != ssh_panes_.end();
}

std::shared_ptr<SshMuxSession> Mux::sshSessionForPane(PaneId pane_id) const {
    auto it = ssh_panes_.find(pane_id);
    if (it == ssh_panes_.end()) return nullptr;
    return it->second.session;
}

int Mux::sshChannelForPane(PaneId pane_id) const {
    auto it = ssh_panes_.find(pane_id);
    if (it == ssh_panes_.end()) return -1;
    return it->second.channel_id;
}

// Zoom, layout presets, and equalize are in mux_layout.cpp

}  // namespace termcore
