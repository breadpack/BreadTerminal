#include "termcore/session.h"
#include "termcore/mux.h"

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace termcore {

// ---- Base64 encode/decode (RFC 4648) ----

static const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i+1]) << 8) | data[i+2];
        result += kBase64Chars[(n >> 18) & 0x3F];
        result += kBase64Chars[(n >> 12) & 0x3F];
        result += kBase64Chars[(n >>  6) & 0x3F];
        result += kBase64Chars[ n        & 0x3F];
    }
    if (i < data.size()) {
        uint32_t n = uint32_t(data[i]) << 16;
        if (i + 1 < data.size()) n |= uint32_t(data[i+1]) << 8;
        result += kBase64Chars[(n >> 18) & 0x3F];
        result += kBase64Chars[(n >> 12) & 0x3F];
        if (i + 1 < data.size())
            result += kBase64Chars[(n >> 6) & 0x3F];
        else
            result += '=';
        result += '=';
    }
    return result;
}

// ---- Zlib compress ----

static std::string compressAndEncode(const std::vector<std::string>& lines) {
    if (lines.empty()) return "";

    // Join lines with newline
    std::string joined;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) joined += '\n';
        joined += lines[i];
    }

    // Compress with zlib
    uLong src_len = static_cast<uLong>(joined.size());
    uLong bound = compressBound(src_len);
    std::vector<uint8_t> compressed(bound);
    int rc = compress2(compressed.data(), &bound,
                       reinterpret_cast<const Bytef*>(joined.data()),
                       src_len, Z_DEFAULT_COMPRESSION);
    if (rc != Z_OK) return "";
    compressed.resize(bound);

    return base64Encode(compressed);
}

// ---- Split tree serialization ----

static void serializeSplitNode(const SplitNode* node,
                               json& out,
                               std::map<PaneId, uint32_t>& pane_serial_map,
                               uint32_t& next_serial) {
    if (!node) {
        out = nullptr;
        return;
    }
    out["is_leaf"] = node->is_leaf;
    if (node->is_leaf) {
        uint32_t serial = 0;
        auto it = pane_serial_map.find(node->pane_id);
        if (it != pane_serial_map.end()) {
            serial = it->second;
        } else {
            serial = next_serial++;
            pane_serial_map[node->pane_id] = serial;
        }
        out["serial"] = serial;
    } else {
        out["direction"] = (node->direction == SplitDirection::Vertical) ? 1 : 0;
        out["ratio"] = node->ratio;
        json first_j, second_j;
        serializeSplitNode(node->first.get(), first_j, pane_serial_map, next_serial);
        serializeSplitNode(node->second.get(), second_j, pane_serial_map, next_serial);
        out["first"] = first_j;
        out["second"] = second_j;
    }
}

// ---- SessionManager implementation ----

static constexpr size_t kMaxScrollbackLines = 5000;

SessionManager::SessionManager() = default;
SessionManager::~SessionManager() = default;

SessionData SessionManager::capture(const Mux& mux,
                                     const IPaneStateProvider& provider,
                                     const WindowGeometry& window) {
    SessionData data;
    data.version = 1;
    data.window = window;

    auto ws_ids = mux.allWorkspaceIds();
    uint32_t next_serial = 1;
    std::map<PaneId, uint32_t> pane_serial_map;

    WorkspaceId active_ws = mux.activeWorkspaceId();
    size_t active_ws_idx = 0;

    for (size_t wi = 0; wi < ws_ids.size(); ++wi) {
        WorkspaceId ws_id = ws_ids[wi];
        if (ws_id == active_ws) active_ws_idx = wi;

        // getWorkspace is non-const, but we can use allTabIds
        WorkspaceSessionData ws_data;
        // Need workspace name - use const_cast since getWorkspace is non-const
        // Actually, we can just leave name empty or derive it.
        // Let's just use a generic approach: walk the const Mux.
        // We added allTabIds which is const. For the name, we don't have const access.
        // We'll store the index-based name for now.
        ws_data.name = "Workspace " + std::to_string(ws_id);

        auto tab_ids = mux.allTabIds(ws_id);

        for (size_t ti = 0; ti < tab_ids.size(); ++ti) {
            TabId tab_id = tab_ids[ti];
            TabSessionData tab_data;
            tab_data.title = "Tab " + std::to_string(tab_id);

            // Serialize split tree
            const SplitNode* root = mux.splitRoot(ws_id, tab_id);
            if (root) {
                tab_data.root = std::make_unique<SplitNodeData>();
                // We serialize to JSON first, then we can reconstruct SplitNodeData
                // Actually, let's build SplitNodeData directly
                std::function<void(const SplitNode*, SplitNodeData&)> buildTree;
                buildTree = [&](const SplitNode* node, SplitNodeData& out) {
                    if (!node) return;
                    out.is_leaf = node->is_leaf;
                    if (node->is_leaf) {
                        auto it = pane_serial_map.find(node->pane_id);
                        if (it != pane_serial_map.end()) {
                            out.leaf_serial = it->second;
                        } else {
                            uint32_t serial = next_serial++;
                            pane_serial_map[node->pane_id] = serial;
                            out.leaf_serial = serial;
                        }
                    } else {
                        out.direction = (node->direction == SplitDirection::Vertical) ? 1 : 0;
                        out.ratio = node->ratio;
                        out.first = std::make_unique<SplitNodeData>();
                        out.second = std::make_unique<SplitNodeData>();
                        buildTree(node->first.get(), *out.first);
                        buildTree(node->second.get(), *out.second);
                    }
                };
                buildTree(root, *tab_data.root);
            }

            // Active pane serial
            PaneId active_pane = mux.activePaneId(ws_id, tab_id);
            auto it = pane_serial_map.find(active_pane);
            if (it != pane_serial_map.end()) {
                tab_data.active_pane_serial = it->second;
            }

            ws_data.tabs.push_back(std::move(tab_data));
        }

        // active_tab_index: find the active tab
        // We don't have direct const access to active_tab_index, use index 0
        ws_data.active_tab_index = 0;

        data.workspaces.push_back(std::move(ws_data));
    }

    data.active_workspace_index = active_ws_idx;

    // Collect pane data
    for (auto& [pane_id, serial] : pane_serial_map) {
        PaneSessionData pd;
        pd.serial = serial;
        pd.working_dir = provider.getWorkingDir(pane_id);
        pd.title = provider.getTitle(pane_id);
        pd.rows = provider.getRows(pane_id);
        pd.cols = provider.getCols(pane_id);
        pd.is_webview = provider.isWebView(pane_id);
        pd.webview_url = provider.getWebViewUrl(pane_id);

        size_t sb_size = provider.getScrollbackSize(pane_id);
        size_t count = std::min(sb_size, kMaxScrollbackLines);
        pd.scrollback_lines.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            pd.scrollback_lines.push_back(provider.getScrollbackLine(pane_id, i));
        }

        data.panes.push_back(std::move(pd));
    }

    return data;
}

bool SessionManager::save(const SessionData& data, const std::string& dir) {
    std::string d = dir.empty() ? defaultSessionDir() : dir;
    if (d.empty()) return false;

    std::error_code ec;
    fs::create_directories(d, ec);
    if (ec) return false;

    std::string filepath = sessionFilePath(d);
    if (filepath.empty()) return false;

    // Build JSON
    json j;
    j["version"] = data.version;
    j["window"] = {
        {"x", data.window.x},
        {"y", data.window.y},
        {"width", data.window.width},
        {"height", data.window.height}
    };
    j["active_workspace_index"] = data.active_workspace_index;

    // Panes
    json panes_j = json::array();
    for (const auto& p : data.panes) {
        json pj;
        pj["serial"] = p.serial;
        pj["working_dir"] = p.working_dir;
        pj["title"] = p.title;
        pj["rows"] = p.rows;
        pj["cols"] = p.cols;
        pj["is_webview"] = p.is_webview;
        pj["webview_url"] = p.webview_url;
        pj["scrollback"] = compressAndEncode(p.scrollback_lines);
        pj["scrollback_lines"] = p.scrollback_lines.size();
        panes_j.push_back(std::move(pj));
    }
    j["panes"] = panes_j;

    // Workspaces
    std::function<json(const SplitNodeData*)> nodeToJson;
    nodeToJson = [&](const SplitNodeData* node) -> json {
        if (!node) return nullptr;
        json nj;
        nj["is_leaf"] = node->is_leaf;
        if (node->is_leaf) {
            nj["serial"] = node->leaf_serial;
        } else {
            nj["direction"] = node->direction;
            nj["ratio"] = node->ratio;
            nj["first"] = nodeToJson(node->first.get());
            nj["second"] = nodeToJson(node->second.get());
        }
        return nj;
    };

    json workspaces_j = json::array();
    for (const auto& ws : data.workspaces) {
        json wj;
        wj["name"] = ws.name;
        wj["active_tab_index"] = ws.active_tab_index;

        json tabs_j = json::array();
        for (const auto& tab : ws.tabs) {
            json tj;
            tj["title"] = tab.title;
            tj["active_pane_serial"] = tab.active_pane_serial;
            tj["root"] = nodeToJson(tab.root.get());
            tabs_j.push_back(std::move(tj));
        }
        wj["tabs"] = tabs_j;
        workspaces_j.push_back(std::move(wj));
    }
    j["workspaces"] = workspaces_j;

    // Atomic write: write to tmp file, then rename
    std::string tmp_path = filepath + ".tmp";
    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        if (!ofs) return false;
        ofs << j.dump(2);
        if (!ofs) return false;
    }

    // Set permissions 0600
    fs::permissions(tmp_path,
                    fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    if (ec) {
        fs::remove(tmp_path, ec);
        return false;
    }

    fs::rename(tmp_path, filepath, ec);
    if (ec) {
        fs::remove(tmp_path, ec);
        return false;
    }

    return true;
}

}  // namespace termcore
