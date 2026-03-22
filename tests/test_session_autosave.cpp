#include "termcore/session_autosave.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

// RAII temp directory
class TempDir {
public:
    TempDir() {
#if defined(_WIN32)
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = (fs::temp_directory_path() /
                 ("bt_autosave_test_" + std::to_string(now)))
                    .string();
        fs::create_directories(path_);
#else
        std::string tmpl =
            (fs::temp_directory_path() / "bt_autosave_test_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        char* result = mkdtemp(buf.data());
        EXPECT_NE(result, nullptr);
        path_ = std::string(buf.data());
#endif
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// Build a minimal SessionData for testing.
termcore::SessionData makeTestSession(int num_tabs = 2, int num_panes = 3) {
    termcore::SessionData data;
    data.version = 2;
    data.window = {10, 20, 1024, 768};
    data.active_workspace_index = 0;

    termcore::WorkspaceSessionData ws;
    ws.name = "TestWorkspace";
    ws.active_tab_index = 0;

    for (int t = 0; t < num_tabs; ++t) {
        termcore::TabSessionData tab;
        tab.title = "Tab " + std::to_string(t);
        tab.active_pane_serial = static_cast<uint32_t>(t * 10 + 1);
        tab.root = std::make_unique<termcore::SplitNodeData>();
        tab.root->is_leaf = true;
        tab.root->leaf_serial = tab.active_pane_serial;
        ws.tabs.push_back(std::move(tab));
    }

    data.workspaces.push_back(std::move(ws));

    for (int p = 0; p < num_panes; ++p) {
        termcore::PaneSessionData pd;
        pd.serial = static_cast<uint32_t>(p + 1);
        pd.working_dir = "/home/user/project" + std::to_string(p);
        pd.title = "pane" + std::to_string(p);
        pd.rows = 24;
        pd.cols = 80;
        pd.is_webview = false;
        pd.profile_id = "default";
        data.panes.push_back(std::move(pd));
    }

    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test: save/load roundtrip
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, SaveLoadRoundtrip) {
    TempDir dir;
    auto original = makeTestSession(2, 3);

    // Save using start + saveNow
    {
        termcore::SessionAutoSave saver;
        saver.start({true, 9999, dir.path()},
                    [&]() { return makeTestSession(2, 3); });
        saver.saveNow(original);
        saver.stop();
    }

    ASSERT_TRUE(
        termcore::SessionAutoSave::hasRecoverableSession(dir.path()));

    auto loaded = termcore::SessionAutoSave::loadLastSession(dir.path());
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->version, original.version);
    EXPECT_EQ(loaded->window.x, original.window.x);
    EXPECT_EQ(loaded->window.y, original.window.y);
    EXPECT_EQ(loaded->window.width, original.window.width);
    EXPECT_EQ(loaded->window.height, original.window.height);
    EXPECT_EQ(loaded->active_workspace_index,
              original.active_workspace_index);
    EXPECT_EQ(loaded->panes.size(), original.panes.size());
    EXPECT_EQ(loaded->workspaces.size(), original.workspaces.size());

    for (size_t i = 0; i < loaded->panes.size(); ++i) {
        EXPECT_EQ(loaded->panes[i].serial, original.panes[i].serial);
        EXPECT_EQ(loaded->panes[i].working_dir,
                  original.panes[i].working_dir);
        EXPECT_EQ(loaded->panes[i].title, original.panes[i].title);
        EXPECT_EQ(loaded->panes[i].rows, original.panes[i].rows);
        EXPECT_EQ(loaded->panes[i].cols, original.panes[i].cols);
        EXPECT_EQ(loaded->panes[i].profile_id,
                  original.panes[i].profile_id);
    }

    for (size_t w = 0; w < loaded->workspaces.size(); ++w) {
        EXPECT_EQ(loaded->workspaces[w].name,
                  original.workspaces[w].name);
        EXPECT_EQ(loaded->workspaces[w].tabs.size(),
                  original.workspaces[w].tabs.size());
    }
}

// ---------------------------------------------------------------------------
// Test: atomic write produces no leftover .tmp file
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, AtomicWriteNoLeftoverTmp) {
    TempDir dir;
    auto data = makeTestSession();

    {
        termcore::SessionAutoSave saver;
        saver.start({true, 9999, dir.path()},
                    [&]() { return makeTestSession(); });
        saver.saveNow(data);
        saver.stop();
    }

    std::string recovery =
        termcore::SessionAutoSave::recoveryFilePath(dir.path());
    std::string tmp = recovery + ".tmp";

    // The final file should exist
    EXPECT_TRUE(fs::exists(recovery));
    // The .tmp file should NOT remain
    EXPECT_FALSE(fs::exists(tmp));
}

// ---------------------------------------------------------------------------
// Test: hasRecoverableSession detection
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, RecoveryDetection) {
    TempDir dir;

    // No file yet
    EXPECT_FALSE(
        termcore::SessionAutoSave::hasRecoverableSession(dir.path()));

    // Save one
    {
        termcore::SessionAutoSave saver;
        auto data = makeTestSession();
        saver.start({true, 9999, dir.path()},
                    [&]() { return makeTestSession(); });
        saver.saveNow(data);
        saver.stop();
    }

    EXPECT_TRUE(
        termcore::SessionAutoSave::hasRecoverableSession(dir.path()));
}

// ---------------------------------------------------------------------------
// Test: clearRecoveryFile on normal exit
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, ClearOnNormalExit) {
    TempDir dir;
    auto data = makeTestSession();

    {
        termcore::SessionAutoSave saver;
        saver.start({true, 9999, dir.path()},
                    [&]() { return makeTestSession(); });
        saver.saveNow(data);
        saver.stop();
    }

    ASSERT_TRUE(
        termcore::SessionAutoSave::hasRecoverableSession(dir.path()));

    termcore::SessionAutoSave::clearRecoveryFile(dir.path());

    EXPECT_FALSE(
        termcore::SessionAutoSave::hasRecoverableSession(dir.path()));
}

// ---------------------------------------------------------------------------
// Test: RecoveryInfo extraction
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, RecoveryInfoExtraction) {
    TempDir dir;
    auto data = makeTestSession(3, 5);

    {
        termcore::SessionAutoSave saver;
        saver.start({true, 9999, dir.path()},
                    [&]() { return makeTestSession(); });
        saver.saveNow(data);
        saver.stop();
    }

    auto info =
        termcore::SessionAutoSave::getRecoveryInfo(dir.path());
    ASSERT_TRUE(info.has_value());

    EXPECT_EQ(info->tab_count, 3);
    EXPECT_EQ(info->pane_count, 5);

    // saved_at should be roughly "now"
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(
                    now - info->saved_at)
                    .count();
    EXPECT_LT(std::abs(diff), 5);  // within 5 seconds
}

// ---------------------------------------------------------------------------
// Test: corrupt file handling
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, CorruptFileHandling) {
    TempDir dir;

    // Write garbage to the recovery file
    std::string filepath =
        termcore::SessionAutoSave::recoveryFilePath(dir.path());
    {
        std::ofstream ofs(filepath);
        ofs << "this is not valid json {{{";
    }

    // hasRecoverableSession returns true (file exists), but load should fail
    EXPECT_TRUE(
        termcore::SessionAutoSave::hasRecoverableSession(dir.path()));

    auto loaded = termcore::SessionAutoSave::loadLastSession(dir.path());
    EXPECT_FALSE(loaded.has_value());

    auto info =
        termcore::SessionAutoSave::getRecoveryInfo(dir.path());
    EXPECT_FALSE(info.has_value());
}

// ---------------------------------------------------------------------------
// Test: empty / missing path handling
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, EmptyPathHandling) {
    EXPECT_FALSE(termcore::SessionAutoSave::hasRecoverableSession(""));
    EXPECT_FALSE(
        termcore::SessionAutoSave::loadLastSession("").has_value());
    EXPECT_FALSE(
        termcore::SessionAutoSave::getRecoveryInfo("").has_value());

    // clearRecoveryFile with empty path should not crash
    termcore::SessionAutoSave::clearRecoveryFile("");
}

// ---------------------------------------------------------------------------
// Test: periodic timer fires at least once
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, PeriodicTimerSaves) {
    TempDir dir;
    auto data = makeTestSession(1, 1);

    {
        termcore::SessionAutoSave saver;
        termcore::AutoSaveConfig config;
        config.enabled = true;
        config.interval_seconds = 1;
        config.save_path = dir.path();

        saver.start(config, [&]() { return makeTestSession(); });

        // Wait long enough for at least one tick
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        saver.stop();
    }

    EXPECT_TRUE(
        termcore::SessionAutoSave::hasRecoverableSession(dir.path()));
}

// ---------------------------------------------------------------------------
// Test: disabled config does not start timer
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, DisabledConfigNoOp) {
    TempDir dir;
    auto data = makeTestSession();

    {
        termcore::SessionAutoSave saver;
        termcore::AutoSaveConfig config;
        config.enabled = false;
        config.interval_seconds = 1;
        config.save_path = dir.path();

        saver.start(config, [&]() { return makeTestSession(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        saver.stop();
    }

    EXPECT_FALSE(
        termcore::SessionAutoSave::hasRecoverableSession(dir.path()));
}

// ---------------------------------------------------------------------------
// Test: invalid version in file returns nullopt
// ---------------------------------------------------------------------------

TEST(SessionAutoSave, InvalidVersionReturnsNullopt) {
    TempDir dir;

    std::string filepath =
        termcore::SessionAutoSave::recoveryFilePath(dir.path());
    {
        std::ofstream ofs(filepath);
        ofs << R"({"version": 99, "recovery": true})";
    }

    auto loaded = termcore::SessionAutoSave::loadLastSession(dir.path());
    EXPECT_FALSE(loaded.has_value());
}
