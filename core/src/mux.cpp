#include "termcore/mux.h"

#include <algorithm>

namespace termcore {

Mux::Mux() = default;
Mux::~Mux() = default;

void Mux::setPaneCallbacks(PaneCreateCallback create_cb, PaneDestroyCallback destroy_cb) {
    create_cb_ = std::move(create_cb);
    destroy_cb_ = std::move(destroy_cb);
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
            return;
        }
    }
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

    return new_pane;
}

void Mux::closePane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab || !tab->root) return;

    // If root is the leaf we want to close, destroy the whole tab's tree
    if (tab->root->is_leaf && tab->root->pane_id == pane_id) {
        if (destroy_cb_) destroy_cb_(pane_id);
        tab->root.reset();
        tab->active_pane = kInvalidPane;
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
}

PaneId Mux::activePaneId(WorkspaceId ws_id, TabId tab_id) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab) return kInvalidPane;
    return tab->active_pane;
}

void Mux::setActivePane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab) return;
    // Verify pane exists in the tree
    if (findNode(tab->root.get(), pane_id)) {
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

}  // namespace termcore
