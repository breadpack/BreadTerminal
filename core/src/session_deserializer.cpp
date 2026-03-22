#include "termcore/session.h"

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace termcore {

// ---- Base64 decode ----

static int base64DecodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<uint8_t> base64Decode(const std::string& encoded) {
    std::vector<uint8_t> result;
    result.reserve((encoded.size() / 4) * 3);
    int val = 0, valb = -8;
    for (char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int d = base64DecodeChar(c);
        if (d < 0) continue;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

// ---- Zlib decompress ----

static std::vector<std::string> decodeAndDecompress(const std::string& encoded,
                                                     size_t expected_lines) {
    if (encoded.empty()) return {};

    auto compressed = base64Decode(encoded);
    if (compressed.empty()) return {};

    // Decompress - start with a reasonable buffer, grow as needed
    std::vector<uint8_t> decompressed;
    size_t buf_size = compressed.size() * 4;
    if (buf_size < 4096) buf_size = 4096;

    for (int attempt = 0; attempt < 10; ++attempt) {
        decompressed.resize(buf_size);
        uLong dest_len = static_cast<uLong>(buf_size);
        int rc = uncompress(decompressed.data(), &dest_len,
                            compressed.data(), static_cast<uLong>(compressed.size()));
        if (rc == Z_OK) {
            decompressed.resize(dest_len);
            break;
        } else if (rc == Z_BUF_ERROR) {
            buf_size *= 2;
            continue;
        } else {
            return {};  // Decompression failed
        }
    }

    // Split into lines
    std::vector<std::string> lines;
    std::string text(decompressed.begin(), decompressed.end());
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(std::move(line));
    }
    // If original text ended with newline, getline doesn't produce trailing empty
    // That's fine for scrollback purposes.

    return lines;
}

// ---- JSON deserialization helpers ----

static std::unique_ptr<SplitNodeData> parseSplitNode(const json& j) {
    if (j.is_null()) return nullptr;

    auto node = std::make_unique<SplitNodeData>();
    node->is_leaf = j.value("is_leaf", true);

    if (node->is_leaf) {
        node->leaf_serial = j.value("serial", 0u);
    } else {
        node->direction = j.value("direction", 0);
        node->ratio = j.value("ratio", 0.5f);
        if (j.contains("first"))
            node->first = parseSplitNode(j["first"]);
        if (j.contains("second"))
            node->second = parseSplitNode(j["second"]);
    }

    return node;
}

Result<SessionData> SessionManager::load(const std::string& dir) {
    std::string filepath = sessionFilePath(dir.empty() ? defaultSessionDir() : dir);
    if (filepath.empty()) return Error("session file path is empty");

    std::error_code ec;
    if (!fs::exists(filepath, ec)) return Error("session file not found: " + filepath);

    try {
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs) return Error("failed to open session file: " + filepath);

        json j = json::parse(ifs);

        SessionData data;
        data.version = j.value("version", 0);
        if (data.version != 1 && data.version != 2) return Error("unsupported session version: " + std::to_string(data.version));

        // Window
        if (j.contains("window")) {
            auto& wj = j["window"];
            data.window.x = wj.value("x", 0);
            data.window.y = wj.value("y", 0);
            data.window.width = wj.value("width", 800);
            data.window.height = wj.value("height", 600);
        }

        data.active_workspace_index = j.value("active_workspace_index", size_t(0));

        // Panes
        if (j.contains("panes") && j["panes"].is_array()) {
            for (auto& pj : j["panes"]) {
                PaneSessionData pd;
                pd.serial = pj.value("serial", 0u);
                pd.working_dir = pj.value("working_dir", "");
                pd.title = pj.value("title", "");
                pd.rows = pj.value("rows", 24);
                pd.cols = pj.value("cols", 80);
                pd.is_webview = pj.value("is_webview", false);
                pd.webview_url = pj.value("webview_url", "");
                pd.profile_id = pj.value("profile_id", "");  // empty for v1

                std::string scrollback_encoded = pj.value("scrollback", "");
                size_t sb_lines = pj.value("scrollback_lines", size_t(0));
                pd.scrollback_lines = decodeAndDecompress(scrollback_encoded, sb_lines);

                data.panes.push_back(std::move(pd));
            }
        }

        // Workspaces
        if (j.contains("workspaces") && j["workspaces"].is_array()) {
            for (auto& wj : j["workspaces"]) {
                WorkspaceSessionData ws;
                ws.name = wj.value("name", "");
                ws.active_tab_index = wj.value("active_tab_index", size_t(0));

                if (wj.contains("tabs") && wj["tabs"].is_array()) {
                    for (auto& tj : wj["tabs"]) {
                        TabSessionData tab;
                        tab.title = tj.value("title", "");
                        tab.active_pane_serial = tj.value("active_pane_serial", 0u);
                        if (tj.contains("root") && !tj["root"].is_null()) {
                            tab.root = parseSplitNode(tj["root"]);
                        }
                        ws.tabs.push_back(std::move(tab));
                    }
                }

                data.workspaces.push_back(std::move(ws));
            }
        }

        return data;

    } catch (const json::exception& e) {
        return Error(std::string("session JSON parse error: ") + e.what());
    } catch (const std::exception& e) {
        return Error(std::string("session load error: ") + e.what());
    }
}

bool SessionManager::hasSavedSession(const std::string& dir) {
    std::string filepath = sessionFilePath(dir.empty() ? defaultSessionDir() : dir);
    if (filepath.empty()) return false;
    std::error_code ec;
    return fs::exists(filepath, ec) && !ec;
}

}  // namespace termcore
