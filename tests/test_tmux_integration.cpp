#include <gtest/gtest.h>
#include "termcore/tmux_integration.h"

using namespace termcore;

// ---------------------------------------------------------------------------
// parseLine tests
// ---------------------------------------------------------------------------

class TmuxParseLineTest : public ::testing::Test {
protected:
    TmuxIntegration tmux_;
};

TEST_F(TmuxParseLineTest, WindowAdd) {
    auto msg = tmux_.parseLine("%window-add @1");
    EXPECT_EQ(msg.type, TmuxNotification::WindowAdd);
    EXPECT_EQ(msg.windowId, 1);
}

TEST_F(TmuxParseLineTest, WindowClose) {
    auto msg = tmux_.parseLine("%window-close @3");
    EXPECT_EQ(msg.type, TmuxNotification::WindowClose);
    EXPECT_EQ(msg.windowId, 3);
}

TEST_F(TmuxParseLineTest, WindowRenamed) {
    auto msg = tmux_.parseLine("%window-renamed @2 my-window");
    EXPECT_EQ(msg.type, TmuxNotification::WindowRenamed);
    EXPECT_EQ(msg.windowId, 2);
    EXPECT_EQ(msg.data, "my-window");
}

TEST_F(TmuxParseLineTest, PaneOutput) {
    auto msg = tmux_.parseLine("%output %5 hello world");
    EXPECT_EQ(msg.type, TmuxNotification::PaneOutput);
    EXPECT_EQ(msg.paneId, 5);
    EXPECT_EQ(msg.data, "hello world");
}

TEST_F(TmuxParseLineTest, LayoutChange) {
    auto msg = tmux_.parseLine("%layout-change @1 abc,80x24,0,0,1");
    EXPECT_EQ(msg.type, TmuxNotification::LayoutChanged);
    EXPECT_EQ(msg.windowId, 1);
    EXPECT_EQ(msg.data, "abc,80x24,0,0,1");
}

TEST_F(TmuxParseLineTest, SessionChanged) {
    auto msg = tmux_.parseLine("%session-changed $1 main");
    EXPECT_EQ(msg.type, TmuxNotification::SessionChanged);
    EXPECT_EQ(msg.data, "$1 main");
}

TEST_F(TmuxParseLineTest, SessionsChanged) {
    auto msg = tmux_.parseLine("%sessions-changed");
    EXPECT_EQ(msg.type, TmuxNotification::SessionsChanged);
}

TEST_F(TmuxParseLineTest, ClientDetached) {
    auto msg = tmux_.parseLine("%client-detached");
    EXPECT_EQ(msg.type, TmuxNotification::ClientDetached);
}

TEST_F(TmuxParseLineTest, Exit) {
    auto msg = tmux_.parseLine("%exit");
    EXPECT_EQ(msg.type, TmuxNotification::Exit);
    EXPECT_EQ(msg.data, "");
}

TEST_F(TmuxParseLineTest, ExitWithReason) {
    auto msg = tmux_.parseLine("%exit server exited");
    EXPECT_EQ(msg.type, TmuxNotification::Exit);
    EXPECT_EQ(msg.data, "server exited");
}

TEST_F(TmuxParseLineTest, UnknownNotification) {
    auto msg = tmux_.parseLine("%begin 1234 0");
    EXPECT_EQ(msg.type, TmuxNotification::Unknown);
}

TEST_F(TmuxParseLineTest, NonNotificationLine) {
    auto msg = tmux_.parseLine("some random output");
    EXPECT_EQ(msg.type, TmuxNotification::Unknown);
}

TEST_F(TmuxParseLineTest, EmptyLine) {
    auto msg = tmux_.parseLine("");
    EXPECT_EQ(msg.type, TmuxNotification::Unknown);
}

TEST_F(TmuxParseLineTest, RawPreserved) {
    std::string line = "%window-add @7";
    auto msg = tmux_.parseLine(line);
    EXPECT_EQ(msg.raw, line);
}

// ---------------------------------------------------------------------------
// feedData tests
// ---------------------------------------------------------------------------

class TmuxFeedDataTest : public ::testing::Test {
protected:
    void SetUp() override {
        windowsAdded_.clear();
        windowsClosed_.clear();

        TmuxCallbacks cb;
        cb.onWindowAdded = [this](const TmuxWindow& w) {
            windowsAdded_.push_back(w.id);
        };
        cb.onWindowClosed = [this](int id) {
            windowsClosed_.push_back(id);
        };
        tmux_.setCallbacks(cb);
    }

    TmuxIntegration tmux_;
    std::vector<int> windowsAdded_;
    std::vector<int> windowsClosed_;
};

TEST_F(TmuxFeedDataTest, SingleCompleteLine) {
    tmux_.feedData("%window-add @1\n");
    ASSERT_EQ(windowsAdded_.size(), 1u);
    EXPECT_EQ(windowsAdded_[0], 1);
}

TEST_F(TmuxFeedDataTest, MultipleLines) {
    tmux_.feedData("%window-add @1\n%window-add @2\n%window-close @1\n");
    EXPECT_EQ(windowsAdded_.size(), 2u);
    EXPECT_EQ(windowsClosed_.size(), 1u);
    EXPECT_EQ(windowsClosed_[0], 1);
}

TEST_F(TmuxFeedDataTest, BufferedPartialLine) {
    tmux_.feedData("%window-add");
    EXPECT_EQ(windowsAdded_.size(), 0u);

    tmux_.feedData(" @1\n");
    EXPECT_EQ(windowsAdded_.size(), 1u);
    EXPECT_EQ(windowsAdded_[0], 1);
}

TEST_F(TmuxFeedDataTest, CrLfHandling) {
    tmux_.feedData("%window-add @1\r\n");
    ASSERT_EQ(windowsAdded_.size(), 1u);
    EXPECT_EQ(windowsAdded_[0], 1);
}

TEST_F(TmuxFeedDataTest, TrailingPartialPreserved) {
    tmux_.feedData("%window-add @1\n%window-add");
    EXPECT_EQ(windowsAdded_.size(), 1u);

    tmux_.feedData(" @2\n");
    EXPECT_EQ(windowsAdded_.size(), 2u);
}

// ---------------------------------------------------------------------------
// Command generation tests
// ---------------------------------------------------------------------------

class TmuxCommandTest : public ::testing::Test {
protected:
    TmuxIntegration tmux_;
};

TEST_F(TmuxCommandTest, SendCommand) {
    EXPECT_EQ(tmux_.sendCommand("list-sessions"), "list-sessions\n");
}

TEST_F(TmuxCommandTest, NewWindowNoName) {
    EXPECT_EQ(tmux_.newWindow(), "new-window\n");
}

TEST_F(TmuxCommandTest, NewWindowWithName) {
    EXPECT_EQ(tmux_.newWindow("build"), "new-window -n \"build\"\n");
}

TEST_F(TmuxCommandTest, CloseWindow) {
    EXPECT_EQ(tmux_.closeWindow(3), "kill-window -t @3\n");
}

TEST_F(TmuxCommandTest, SelectWindow) {
    EXPECT_EQ(tmux_.selectWindow(2), "select-window -t @2\n");
}

TEST_F(TmuxCommandTest, SplitPaneHorizontal) {
    EXPECT_EQ(tmux_.splitPane(1, true), "split-window -h -t %1\n");
}

TEST_F(TmuxCommandTest, SplitPaneVertical) {
    EXPECT_EQ(tmux_.splitPane(1, false), "split-window -v -t %1\n");
}

TEST_F(TmuxCommandTest, ClosePane) {
    EXPECT_EQ(tmux_.closePane(4), "kill-pane -t %4\n");
}

TEST_F(TmuxCommandTest, SelectPane) {
    EXPECT_EQ(tmux_.selectPane(2), "select-pane -t %2\n");
}

TEST_F(TmuxCommandTest, RenameWindow) {
    EXPECT_EQ(tmux_.renameWindow(1, "editor"), "rename-window -t @1 \"editor\"\n");
}

TEST_F(TmuxCommandTest, ListWindows) {
    EXPECT_EQ(tmux_.listWindows(), "list-windows\n");
}

TEST_F(TmuxCommandTest, ListPanes) {
    EXPECT_EQ(tmux_.listPanes(1), "list-panes -t @1\n");
}

TEST_F(TmuxCommandTest, SendKeys) {
    EXPECT_EQ(tmux_.sendKeys(0, "ls Enter"), "send-keys -t %0 ls Enter\n");
}

TEST_F(TmuxCommandTest, ResizePane) {
    EXPECT_EQ(tmux_.resizePane(1, 120, 40), "resize-pane -t %1 -x 120 -y 40\n");
}

// ---------------------------------------------------------------------------
// isControlModeStart tests
// ---------------------------------------------------------------------------

TEST(TmuxControlModeStartTest, BeginLine) {
    EXPECT_TRUE(TmuxIntegration::isControlModeStart("%begin 1234 0"));
}

TEST(TmuxControlModeStartTest, DCSSequence) {
    EXPECT_TRUE(TmuxIntegration::isControlModeStart("\033P1000p"));
}

TEST(TmuxControlModeStartTest, RegularLine) {
    EXPECT_FALSE(TmuxIntegration::isControlModeStart("some output"));
}

TEST(TmuxControlModeStartTest, EmptyLine) {
    EXPECT_FALSE(TmuxIntegration::isControlModeStart(""));
}

// ---------------------------------------------------------------------------
// State tracking tests
// ---------------------------------------------------------------------------

class TmuxStateTest : public ::testing::Test {
protected:
    TmuxIntegration tmux_;
};

TEST_F(TmuxStateTest, InitiallyInactive) {
    EXPECT_FALSE(tmux_.isActive());
    EXPECT_TRUE(tmux_.windows().empty());
    EXPECT_TRUE(tmux_.panes().empty());
}

TEST_F(TmuxStateTest, SetActive) {
    tmux_.setActive(true);
    EXPECT_TRUE(tmux_.isActive());
    tmux_.setActive(false);
    EXPECT_FALSE(tmux_.isActive());
}

TEST_F(TmuxStateTest, WindowAddUpdatesState) {
    tmux_.feedData("%window-add @1\n%window-add @2\n");
    EXPECT_EQ(tmux_.windows().size(), 2u);
    EXPECT_NE(tmux_.windows().find(1), tmux_.windows().end());
    EXPECT_NE(tmux_.windows().find(2), tmux_.windows().end());
}

TEST_F(TmuxStateTest, WindowCloseUpdatesState) {
    tmux_.feedData("%window-add @1\n%window-add @2\n%window-close @1\n");
    EXPECT_EQ(tmux_.windows().size(), 1u);
    EXPECT_EQ(tmux_.windows().find(1), tmux_.windows().end());
    EXPECT_NE(tmux_.windows().find(2), tmux_.windows().end());
}

TEST_F(TmuxStateTest, WindowRenameUpdatesState) {
    tmux_.feedData("%window-add @1\n%window-renamed @1 new-name\n");
    ASSERT_NE(tmux_.windows().find(1), tmux_.windows().end());
    EXPECT_EQ(tmux_.windows().at(1).name, "new-name");
}

TEST_F(TmuxStateTest, ClientDetachedDeactivates) {
    tmux_.setActive(true);
    tmux_.feedData("%client-detached\n");
    EXPECT_FALSE(tmux_.isActive());
}

TEST_F(TmuxStateTest, ExitDeactivates) {
    tmux_.setActive(true);
    tmux_.feedData("%exit\n");
    EXPECT_FALSE(tmux_.isActive());
}

// ---------------------------------------------------------------------------
// Callback tests
// ---------------------------------------------------------------------------

class TmuxCallbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        renamedWindows_.clear();
        paneOutputs_.clear();
        layoutChanges_.clear();
        detachCount_ = 0;

        TmuxCallbacks cb;
        cb.onWindowRenamed = [this](int id, const std::string& name) {
            renamedWindows_.push_back({id, name});
        };
        cb.onPaneOutput = [this](int id, const std::string& data) {
            paneOutputs_.push_back({id, data});
        };
        cb.onLayoutChanged = [this](const std::string& layout) {
            layoutChanges_.push_back(layout);
        };
        cb.onDetached = [this]() { detachCount_++; };
        tmux_.setCallbacks(cb);
    }

    TmuxIntegration tmux_;
    std::vector<std::pair<int, std::string>> renamedWindows_;
    std::vector<std::pair<int, std::string>> paneOutputs_;
    std::vector<std::string> layoutChanges_;
    int detachCount_ = 0;
};

TEST_F(TmuxCallbackTest, WindowRenamedCallback) {
    tmux_.feedData("%window-add @1\n%window-renamed @1 dev\n");
    ASSERT_EQ(renamedWindows_.size(), 1u);
    EXPECT_EQ(renamedWindows_[0].first, 1);
    EXPECT_EQ(renamedWindows_[0].second, "dev");
}

TEST_F(TmuxCallbackTest, PaneOutputCallback) {
    tmux_.feedData("%output %3 test data here\n");
    ASSERT_EQ(paneOutputs_.size(), 1u);
    EXPECT_EQ(paneOutputs_[0].first, 3);
    EXPECT_EQ(paneOutputs_[0].second, "test data here");
}

TEST_F(TmuxCallbackTest, LayoutChangedCallback) {
    tmux_.feedData("%layout-change @0 d9a0,204x51,0,0,0\n");
    ASSERT_EQ(layoutChanges_.size(), 1u);
    EXPECT_EQ(layoutChanges_[0], "d9a0,204x51,0,0,0");
}

TEST_F(TmuxCallbackTest, DetachCallbackOnClientDetached) {
    tmux_.feedData("%client-detached\n");
    EXPECT_EQ(detachCount_, 1);
}

TEST_F(TmuxCallbackTest, DetachCallbackOnExit) {
    tmux_.feedData("%exit\n");
    EXPECT_EQ(detachCount_, 1);
}
