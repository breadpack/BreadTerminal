#include "termcore/session.h"
#include "termcore/mux.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// RAII temp directory
class TempDir {
public:
    TempDir() {
        std::string tmpl = (fs::temp_directory_path() / "bt_session_test_XXXXXX").string();
        // mkdtemp modifies the template in-place
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* result = mkdtemp(buf.data());
        EXPECT_NE(result, nullptr);
        path_ = std::string(buf.data());
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// Mock pane state provider
class MockPaneProvider : public termcore::IPaneStateProvider {
public:
    struct PaneInfo {
        std::string working_dir;
        std::string title;
        int rows = 24;
        int cols = 80;
        std::vector<std::string> scrollback;
        bool is_webview = false;
        std::string webview_url;
    };

    std::map<uint32_t, PaneInfo> panes;

    std::string getWorkingDir(uint32_t pane_id) const override {
        auto it = panes.find(pane_id);
        return (it != panes.end()) ? it->second.working_dir : "";
    }

    std::string getTitle(uint32_t pane_id) const override {
        auto it = panes.find(pane_id);
        return (it != panes.end()) ? it->second.title : "";
    }

    int getRows(uint32_t pane_id) const override {
        auto it = panes.find(pane_id);
        return (it != panes.end()) ? it->second.rows : 24;
    }

    int getCols(uint32_t pane_id) const override {
        auto it = panes.find(pane_id);
        return (it != panes.end()) ? it->second.cols : 80;
    }

    size_t getScrollbackSize(uint32_t pane_id) const override {
        auto it = panes.find(pane_id);
        return (it != panes.end()) ? it->second.scrollback.size() : 0;
    }

    std::string getScrollbackLine(uint32_t pane_id, size_t line) const override {
        auto it = panes.find(pane_id);
        if (it == panes.end() || line >= it->second.scrollback.size()) return "";
        return it->second.scrollback[line];
    }

    bool isWebView(uint32_t pane_id) const override {
        auto it = panes.find(pane_id);
        return (it != panes.end()) ? it->second.is_webview : false;
    }

    std::string getWebViewUrl(uint32_t pane_id) const override {
        auto it = panes.find(pane_id);
        return (it != panes.end()) ? it->second.webview_url : "";
    }
};

}  // namespace

// Test: empty session save and load round-trip
TEST(SessionTest, EmptySessionSaveLoad) {
    TempDir tmp;
    termcore::SessionManager mgr;

    termcore::SessionData data;
    data.version = 1;
    data.window = {100, 200, 1024, 768};

    ASSERT_TRUE(mgr.save(data, tmp.path()));
    ASSERT_TRUE(mgr.hasSavedSession(tmp.path()));

    auto loaded = mgr.load(tmp.path());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->version, 1);
    EXPECT_EQ(loaded->window.x, 100);
    EXPECT_EQ(loaded->window.y, 200);
    EXPECT_EQ(loaded->window.width, 1024);
    EXPECT_EQ(loaded->window.height, 768);
    EXPECT_TRUE(loaded->workspaces.empty());
    EXPECT_TRUE(loaded->panes.empty());
}

// Test: scrollback compress/decompress round-trip
TEST(SessionTest, ScrollbackRoundTrip) {
    TempDir tmp;
    termcore::SessionManager mgr;

    termcore::Mux mux;
    uint32_t next_pane_id = 1;
    mux.setPaneCallbacks(
        [&](int, int) -> termcore::PaneId { return next_pane_id++; },
        [](termcore::PaneId) {});

    auto ws_id = mux.createWorkspace("test");
    auto tab_id = mux.createTab(ws_id);

    MockPaneProvider provider;
    provider.panes[1] = {
        "/home/user",
        "bash",
        24, 80,
        {"line 1", "line 2 with special chars: !@#$%^&*()", "line 3: unicode \xC3\xA9\xC3\xA0\xC3\xBC", "", "line 5"},
        false,
        ""
    };

    auto data = mgr.capture(mux, provider);
    ASSERT_TRUE(mgr.save(data, tmp.path()));

    auto loaded = mgr.load(tmp.path());
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->panes.size(), 1u);

    auto& pane = loaded->panes[0];
    EXPECT_EQ(pane.working_dir, "/home/user");
    EXPECT_EQ(pane.title, "bash");
    EXPECT_EQ(pane.rows, 24);
    EXPECT_EQ(pane.cols, 80);
    ASSERT_EQ(pane.scrollback_lines.size(), 5u);
    EXPECT_EQ(pane.scrollback_lines[0], "line 1");
    EXPECT_EQ(pane.scrollback_lines[1], "line 2 with special chars: !@#$%^&*()");
    EXPECT_EQ(pane.scrollback_lines[2], "line 3: unicode \xC3\xA9\xC3\xA0\xC3\xBC");
    EXPECT_EQ(pane.scrollback_lines[3], "");
    EXPECT_EQ(pane.scrollback_lines[4], "line 5");
}

// Test: split tree serialization
TEST(SessionTest, SplitTreeSerialization) {
    TempDir tmp;
    termcore::SessionManager mgr;

    termcore::Mux mux;
    uint32_t next_pane_id = 1;
    mux.setPaneCallbacks(
        [&](int, int) -> termcore::PaneId { return next_pane_id++; },
        [](termcore::PaneId) {});

    auto ws_id = mux.createWorkspace("ws1");
    auto tab_id = mux.createTab(ws_id);

    // Split: pane 1 gets split, producing pane 2
    auto pane2 = mux.splitPane(ws_id, tab_id, 1, termcore::SplitDirection::Vertical);
    ASSERT_NE(pane2, termcore::kInvalidPane);

    MockPaneProvider provider;
    provider.panes[1] = {"/tmp", "pane1", 24, 40, {}, false, ""};
    provider.panes[2] = {"/home", "pane2", 24, 40, {}, false, ""};

    auto data = mgr.capture(mux, provider);
    ASSERT_TRUE(mgr.save(data, tmp.path()));

    auto loaded = mgr.load(tmp.path());
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->workspaces.size(), 1u);
    ASSERT_EQ(loaded->workspaces[0].tabs.size(), 1u);

    auto& root = loaded->workspaces[0].tabs[0].root;
    ASSERT_NE(root, nullptr);
    EXPECT_FALSE(root->is_leaf);
    EXPECT_EQ(root->direction, 1);  // Vertical
    EXPECT_FLOAT_EQ(root->ratio, 0.5f);

    ASSERT_NE(root->first, nullptr);
    EXPECT_TRUE(root->first->is_leaf);

    ASSERT_NE(root->second, nullptr);
    EXPECT_TRUE(root->second->is_leaf);

    // Both leaf serials should reference panes
    EXPECT_NE(root->first->leaf_serial, root->second->leaf_serial);
    EXPECT_EQ(loaded->panes.size(), 2u);
}

// Test: window geometry
TEST(SessionTest, WindowGeometry) {
    TempDir tmp;
    termcore::SessionManager mgr;

    termcore::SessionData data;
    data.version = 1;
    data.window = {42, 99, 1920, 1080};

    ASSERT_TRUE(mgr.save(data, tmp.path()));
    auto loaded = mgr.load(tmp.path());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->window.x, 42);
    EXPECT_EQ(loaded->window.y, 99);
    EXPECT_EQ(loaded->window.width, 1920);
    EXPECT_EQ(loaded->window.height, 1080);
}

// Test: invalid JSON returns nullopt
TEST(SessionTest, InvalidJsonReturnsNullopt) {
    TempDir tmp;
    termcore::SessionManager mgr;

    // Write garbage
    std::string filepath = termcore::SessionManager::sessionFilePath(tmp.path());
    std::ofstream ofs(filepath);
    ofs << "this is not json {{{";
    ofs.close();

    auto loaded = mgr.load(tmp.path());
    EXPECT_FALSE(loaded.has_value());
}

// Test: unknown version returns nullopt
TEST(SessionTest, UnknownVersionReturnsNullopt) {
    TempDir tmp;
    termcore::SessionManager mgr;

    std::string filepath = termcore::SessionManager::sessionFilePath(tmp.path());
    // Create the directory first
    std::error_code ec;
    fs::create_directories(tmp.path(), ec);

    std::ofstream ofs(filepath);
    ofs << R"({"version": 999})";
    ofs.close();

    auto loaded = mgr.load(tmp.path());
    EXPECT_FALSE(loaded.has_value());
}

// Test: file permissions are 0600
TEST(SessionTest, FilePermissions) {
    TempDir tmp;
    termcore::SessionManager mgr;

    termcore::SessionData data;
    data.version = 1;
    ASSERT_TRUE(mgr.save(data, tmp.path()));

    std::string filepath = termcore::SessionManager::sessionFilePath(tmp.path());
    auto status = fs::status(filepath);
    auto perms = status.permissions();

    // Owner read+write
    EXPECT_NE(perms & fs::perms::owner_read, fs::perms::none);
    EXPECT_NE(perms & fs::perms::owner_write, fs::perms::none);
    // No group or other
    EXPECT_EQ(perms & fs::perms::group_read, fs::perms::none);
    EXPECT_EQ(perms & fs::perms::group_write, fs::perms::none);
    EXPECT_EQ(perms & fs::perms::others_read, fs::perms::none);
    EXPECT_EQ(perms & fs::perms::others_write, fs::perms::none);
}

// Test: no saved session
TEST(SessionTest, NoSavedSession) {
    TempDir tmp;
    termcore::SessionManager mgr;

    EXPECT_FALSE(mgr.hasSavedSession(tmp.path()));
    auto loaded = mgr.load(tmp.path());
    EXPECT_FALSE(loaded.has_value());
}

// Test: webview pane data
TEST(SessionTest, WebViewPaneData) {
    TempDir tmp;
    termcore::SessionManager mgr;

    termcore::Mux mux;
    uint32_t next_pane_id = 1;
    mux.setPaneCallbacks(
        [&](int, int) -> termcore::PaneId { return next_pane_id++; },
        [](termcore::PaneId) {});

    auto ws_id = mux.createWorkspace("test");
    auto tab_id = mux.createTab(ws_id);

    MockPaneProvider provider;
    provider.panes[1] = {
        "", "Google", 0, 0, {},
        true, "https://www.google.com"
    };

    auto data = mgr.capture(mux, provider);
    ASSERT_TRUE(mgr.save(data, tmp.path()));

    auto loaded = mgr.load(tmp.path());
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->panes.size(), 1u);
    EXPECT_TRUE(loaded->panes[0].is_webview);
    EXPECT_EQ(loaded->panes[0].webview_url, "https://www.google.com");
    EXPECT_EQ(loaded->panes[0].title, "Google");
}

// Test: multiple workspaces and tabs
TEST(SessionTest, MultipleWorkspacesAndTabs) {
    TempDir tmp;
    termcore::SessionManager mgr;

    termcore::Mux mux;
    uint32_t next_pane_id = 1;
    mux.setPaneCallbacks(
        [&](int, int) -> termcore::PaneId { return next_pane_id++; },
        [](termcore::PaneId) {});

    auto ws1 = mux.createWorkspace("ws1");
    auto ws2 = mux.createWorkspace("ws2");
    auto tab1 = mux.createTab(ws1);
    auto tab2 = mux.createTab(ws1);
    auto tab3 = mux.createTab(ws2);

    mux.setActiveWorkspace(ws2);

    MockPaneProvider provider;
    provider.panes[1] = {"/tmp/1", "t1", 24, 80, {}, false, ""};
    provider.panes[2] = {"/tmp/2", "t2", 24, 80, {}, false, ""};
    provider.panes[3] = {"/tmp/3", "t3", 24, 80, {}, false, ""};

    auto data = mgr.capture(mux, provider);
    ASSERT_TRUE(mgr.save(data, tmp.path()));

    auto loaded = mgr.load(tmp.path());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->workspaces.size(), 2u);
    EXPECT_EQ(loaded->workspaces[0].tabs.size(), 2u);
    EXPECT_EQ(loaded->workspaces[1].tabs.size(), 1u);
    EXPECT_EQ(loaded->active_workspace_index, 1u);  // ws2 is active
    EXPECT_EQ(loaded->panes.size(), 3u);
}
