#include "termcore/mux.h"

#include <algorithm>
#include <cmath>

namespace termcore {

// --- Zoom ---

void Mux::toggleZoom(WorkspaceId ws_id, TabId tab_id) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab) return;

    if (tab->zoomed_pane != kInvalidPane) {
        tab->zoomed_pane = kInvalidPane;
    } else {
        if (tab->active_pane != kInvalidPane) {
            tab->zoomed_pane = tab->active_pane;
        }
    }
    fireOnChanged();
}

bool Mux::isZoomed(WorkspaceId ws_id, TabId tab_id) const {
    const auto* tab = findTab(ws_id, tab_id);
    if (!tab) return false;
    return tab->zoomed_pane != kInvalidPane;
}

PaneId Mux::zoomedPane(WorkspaceId ws_id, TabId tab_id) const {
    const auto* tab = findTab(ws_id, tab_id);
    if (!tab) return kInvalidPane;
    return tab->zoomed_pane;
}

// --- Layout Presets ---

std::unique_ptr<SplitNode> Mux::buildEvenChain(const std::vector<PaneId>& panes,
                                                 SplitDirection direction) {
    if (panes.empty()) return nullptr;

    if (panes.size() == 1) {
        auto node = std::make_unique<SplitNode>();
        node->is_leaf = true;
        node->pane_id = panes[0];
        return node;
    }

    // Build a chain: first pane on one side, rest on the other
    auto node = std::make_unique<SplitNode>();
    node->is_leaf = false;
    node->direction = direction;
    node->ratio = 1.0f / static_cast<float>(panes.size());

    auto first = std::make_unique<SplitNode>();
    first->is_leaf = true;
    first->pane_id = panes[0];
    node->first = std::move(first);

    std::vector<PaneId> rest(panes.begin() + 1, panes.end());
    node->second = buildEvenChain(rest, direction);

    return node;
}

std::unique_ptr<SplitNode> Mux::buildTiled(const std::vector<PaneId>& panes) {
    if (panes.empty()) return nullptr;

    if (panes.size() == 1) {
        auto node = std::make_unique<SplitNode>();
        node->is_leaf = true;
        node->pane_id = panes[0];
        return node;
    }

    size_t n = panes.size();
    size_t cols = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(n))));
    size_t rows = static_cast<size_t>(std::ceil(static_cast<double>(n) / static_cast<double>(cols)));

    // Build rows, each row is an EvenHorizontal chain
    std::vector<std::unique_ptr<SplitNode>> row_nodes;
    size_t idx = 0;
    for (size_t r = 0; r < rows && idx < n; ++r) {
        size_t row_count = std::min(cols, n - idx);
        std::vector<PaneId> row_panes(panes.begin() + static_cast<ptrdiff_t>(idx),
                                       panes.begin() + static_cast<ptrdiff_t>(idx + row_count));
        row_nodes.push_back(buildEvenChain(row_panes, SplitDirection::Vertical));
        idx += row_count;
    }

    // Stack rows vertically using EvenVertical-style chain
    if (row_nodes.size() == 1) {
        return std::move(row_nodes[0]);
    }

    // Build chain from row_nodes
    auto result = std::move(row_nodes.back());
    for (size_t i = row_nodes.size() - 1; i > 0; --i) {
        auto node = std::make_unique<SplitNode>();
        node->is_leaf = false;
        node->direction = SplitDirection::Horizontal;
        node->ratio = 1.0f / static_cast<float>(i + 1);
        node->first = std::move(row_nodes[i - 1]);
        node->second = std::move(result);
        result = std::move(node);
    }

    return result;
}

std::unique_ptr<SplitNode> Mux::buildLayoutTree(const std::vector<PaneId>& panes,
                                                  LayoutPreset preset) {
    if (panes.empty()) return nullptr;

    if (panes.size() == 1) {
        auto node = std::make_unique<SplitNode>();
        node->is_leaf = true;
        node->pane_id = panes[0];
        return node;
    }

    switch (preset) {
        case LayoutPreset::EvenHorizontal:
            return buildEvenChain(panes, SplitDirection::Vertical);

        case LayoutPreset::EvenVertical:
            return buildEvenChain(panes, SplitDirection::Horizontal);

        case LayoutPreset::Tiled:
            return buildTiled(panes);

        case LayoutPreset::MainLeft: {
            auto node = std::make_unique<SplitNode>();
            node->is_leaf = false;
            node->direction = SplitDirection::Vertical;
            node->ratio = 0.6f;

            auto first = std::make_unique<SplitNode>();
            first->is_leaf = true;
            first->pane_id = panes[0];
            node->first = std::move(first);

            std::vector<PaneId> rest(panes.begin() + 1, panes.end());
            node->second = buildEvenChain(rest, SplitDirection::Horizontal);

            return node;
        }

        case LayoutPreset::MainTop: {
            auto node = std::make_unique<SplitNode>();
            node->is_leaf = false;
            node->direction = SplitDirection::Horizontal;
            node->ratio = 0.6f;

            auto first = std::make_unique<SplitNode>();
            first->is_leaf = true;
            first->pane_id = panes[0];
            node->first = std::move(first);

            std::vector<PaneId> rest(panes.begin() + 1, panes.end());
            node->second = buildEvenChain(rest, SplitDirection::Vertical);

            return node;
        }
    }

    return nullptr;
}

void Mux::applyLayout(WorkspaceId ws_id, TabId tab_id, LayoutPreset preset) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab || !tab->root) return;

    // Unzoom first
    tab->zoomed_pane = kInvalidPane;

    // Collect all pane IDs
    std::vector<PaneId> panes;
    collectPanes(tab->root.get(), panes);
    if (panes.empty()) return;

    // Build new tree
    tab->root = buildLayoutTree(panes, preset);

    // Update current_layout index
    tab->current_layout = static_cast<int>(preset);

    fireOnChanged();
}

void Mux::nextLayout(WorkspaceId ws_id, TabId tab_id) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab) return;

    int next = (tab->current_layout + 1) % 5;
    applyLayout(ws_id, tab_id, static_cast<LayoutPreset>(next));
}

// --- Equalize Splits ---

void Mux::equalizeSplitsRecursive(SplitNode* node) {
    if (!node || node->is_leaf) return;
    node->ratio = 0.5f;
    equalizeSplitsRecursive(node->first.get());
    equalizeSplitsRecursive(node->second.get());
}

void Mux::equalizeSplits(WorkspaceId ws_id, TabId tab_id) {
    auto* tab = findTab(ws_id, tab_id);
    if (!tab || !tab->root) return;

    equalizeSplitsRecursive(tab->root.get());
    fireOnChanged();
}

}  // namespace termcore
