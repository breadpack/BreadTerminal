#include <gtest/gtest.h>
#include "termcore/socket/command_dispatcher.h"

using namespace termcore;

class CommandDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        next_pane_ = 1;
        destroyed_.clear();
        write_calls_.clear();
        webview_calls_.clear();

        mux_.setPaneCallbacks(
            [this](int, int) -> PaneId { return next_pane_++; },
            [this](PaneId id) { destroyed_.push_back(id); }
        );

        dispatcher_ = std::make_unique<CommandDispatcher>(
            mux_, notifications_, agent_tracker_,
            [this](PaneId id, std::string_view data) -> bool {
                write_calls_.push_back({id, std::string(data)});
                return true;
            },
            [this](const std::string& method, const nlohmann::json& params) {
                webview_calls_.push_back({method, params});
            }
        );
    }

    rpc::Response dispatch(const std::string& method, nlohmann::json params = {}, int64_t id = 1) {
        rpc::Request req;
        req.method = method;
        req.params = std::move(params);
        req.id = id;
        return dispatcher_->dispatch(req);
    }

    Mux mux_;
    NotificationStore notifications_;
    AgentTracker agent_tracker_;
    std::unique_ptr<CommandDispatcher> dispatcher_;

    uint32_t next_pane_ = 1;
    std::vector<PaneId> destroyed_;
    std::vector<std::pair<PaneId, std::string>> write_calls_;
    std::vector<std::pair<std::string, nlohmann::json>> webview_calls_;
};

// --- Unknown method ---

TEST_F(CommandDispatcherTest, UnknownMethod) {
    auto resp = dispatch("unknown.method");
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kMethodNotFound);
}

// --- workspace tests ---

TEST_F(CommandDispatcherTest, WorkspaceCreate) {
    auto resp = dispatch("workspace.create", {{"name", "test"}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result.contains("workspace_id"));
    EXPECT_EQ(resp.result["name"], "test");
}

TEST_F(CommandDispatcherTest, WorkspaceCreateNoName) {
    auto resp = dispatch("workspace.create");
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result.contains("workspace_id"));
}

TEST_F(CommandDispatcherTest, WorkspaceList) {
    dispatch("workspace.create", {{"name", "ws1"}});
    dispatch("workspace.create", {{"name", "ws2"}});

    auto resp = dispatch("workspace.list");
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["workspaces"].size(), 2u);
}

TEST_F(CommandDispatcherTest, WorkspaceSwitch) {
    auto create_resp = dispatch("workspace.create", {{"name", "ws1"}});
    auto ws_id = create_resp.result["workspace_id"].get<WorkspaceId>();

    auto resp = dispatch("workspace.switch", {{"workspace_id", ws_id}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result["success"].get<bool>());
}

TEST_F(CommandDispatcherTest, WorkspaceSwitchNotFound) {
    auto resp = dispatch("workspace.switch", {{"workspace_id", 999}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kNotFound);
}

TEST_F(CommandDispatcherTest, WorkspaceSwitchMissingParam) {
    auto resp = dispatch("workspace.switch", {});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

TEST_F(CommandDispatcherTest, WorkspaceDestroy) {
    auto create_resp = dispatch("workspace.create", {{"name", "ws1"}});
    auto ws_id = create_resp.result["workspace_id"].get<WorkspaceId>();

    auto resp = dispatch("workspace.destroy", {{"workspace_id", ws_id}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result["success"].get<bool>());
}

// --- tab tests ---

TEST_F(CommandDispatcherTest, TabCreate) {
    auto ws_resp = dispatch("workspace.create", {{"name", "ws1"}});
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();

    auto resp = dispatch("tab.create", {{"workspace_id", ws_id}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result.contains("tab_id"));
}

TEST_F(CommandDispatcherTest, TabList) {
    auto ws_resp = dispatch("workspace.create", {{"name", "ws1"}});
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    dispatch("tab.create", {{"workspace_id", ws_id}});

    auto resp = dispatch("tab.list", {{"workspace_id", ws_id}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_GE(resp.result["tabs"].size(), 1u);
}

TEST_F(CommandDispatcherTest, TabClose) {
    auto ws_resp = dispatch("workspace.create", {{"name", "ws1"}});
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    auto tab_resp = dispatch("tab.create", {{"workspace_id", ws_id}});
    auto tab_id = tab_resp.result["tab_id"].get<TabId>();

    auto resp = dispatch("tab.close", {{"workspace_id", ws_id}, {"tab_id", tab_id}});
    ASSERT_FALSE(resp.error.has_value());
}

// --- pane tests ---

TEST_F(CommandDispatcherTest, PaneSplit) {
    auto ws_resp = dispatch("workspace.create");
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    auto tab_resp = dispatch("tab.create", {{"workspace_id", ws_id}});
    auto tab_id = tab_resp.result["tab_id"].get<TabId>();

    auto panes_resp = dispatch("pane.list", {{"workspace_id", ws_id}, {"tab_id", tab_id}});
    ASSERT_FALSE(panes_resp.error.has_value());
    auto pane_id = panes_resp.result["panes"][0]["id"].get<PaneId>();

    auto resp = dispatch("pane.split", {
        {"workspace_id", ws_id}, {"tab_id", tab_id},
        {"pane_id", pane_id}, {"direction", "vertical"}
    });
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result.contains("pane_id"));
}

TEST_F(CommandDispatcherTest, PaneSplitMissingParams) {
    auto resp = dispatch("pane.split", {{"workspace_id", 1}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

TEST_F(CommandDispatcherTest, PaneList) {
    auto ws_resp = dispatch("workspace.create");
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    auto tab_resp = dispatch("tab.create", {{"workspace_id", ws_id}});
    auto tab_id = tab_resp.result["tab_id"].get<TabId>();

    auto resp = dispatch("pane.list", {{"workspace_id", ws_id}, {"tab_id", tab_id}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_GE(resp.result["panes"].size(), 1u);
}

TEST_F(CommandDispatcherTest, PaneSendText) {
    auto resp = dispatch("pane.send-text", {{"pane_id", 1}, {"text", "hello\n"}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["bytes_written"], 6);
    ASSERT_EQ(write_calls_.size(), 1u);
    EXPECT_EQ(write_calls_[0].first, 1u);
    EXPECT_EQ(write_calls_[0].second, "hello\n");
}

TEST_F(CommandDispatcherTest, PaneSendTextMissingParams) {
    auto resp = dispatch("pane.send-text", {{"pane_id", 1}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

TEST_F(CommandDispatcherTest, PaneSendKeys) {
    auto resp = dispatch("pane.send-keys", {
        {"pane_id", 1}, {"keys", {"ctrl+c", "enter"}}
    });
    ASSERT_FALSE(resp.error.has_value());
    ASSERT_EQ(write_calls_.size(), 1u);
    EXPECT_EQ(write_calls_[0].second, std::string("\x03\r"));
}

// --- notify tests ---

TEST_F(CommandDispatcherTest, NotifySend) {
    auto resp = dispatch("notify.send", {
        {"title", "Build Done"}, {"body", "All tests passed"}, {"urgency", "normal"}
    });
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result.contains("notification_id"));
    EXPECT_EQ(notifications_.count(), 1u);
}

TEST_F(CommandDispatcherTest, NotifySendMissingTitle) {
    auto resp = dispatch("notify.send", {{"body", "no title"}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

// --- browser tests ---

TEST_F(CommandDispatcherTest, BrowserOpen) {
    auto resp = dispatch("browser.open", {{"url", "https://example.com"}});
    ASSERT_FALSE(resp.error.has_value());
    ASSERT_EQ(webview_calls_.size(), 1u);
    EXPECT_EQ(webview_calls_[0].first, "navigate");
}

TEST_F(CommandDispatcherTest, BrowserOpenNoWebview) {
    // Create dispatcher without webview callback
    auto dispatcher_no_wv = std::make_unique<CommandDispatcher>(
        mux_, notifications_, agent_tracker_,
        nullptr, nullptr);
    rpc::Request req;
    req.method = "browser.open";
    req.params = {{"url", "https://example.com"}};
    req.id = 1;
    auto resp = dispatcher_no_wv->dispatch(req);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kNotFound);
}

// --- query tests ---

TEST_F(CommandDispatcherTest, QueryActivePaneNoWorkspace) {
    auto resp = dispatch("query.active-pane");
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kNotFound);
}

TEST_F(CommandDispatcherTest, QueryActivePane) {
    auto ws_resp = dispatch("workspace.create");
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    dispatch("workspace.switch", {{"workspace_id", ws_id}});
    dispatch("tab.create", {{"workspace_id", ws_id}});

    auto resp = dispatch("query.active-pane");
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result.contains("workspace_id"));
    EXPECT_TRUE(resp.result.contains("tab_id"));
    EXPECT_TRUE(resp.result.contains("pane_id"));
}

TEST_F(CommandDispatcherTest, QueryAgentStateAllEmpty) {
    auto resp = dispatch("query.agent-state");
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result["agents"].is_array());
    EXPECT_EQ(resp.result["agents"].size(), 0u);
}

TEST_F(CommandDispatcherTest, QueryAgentStateNotFound) {
    auto resp = dispatch("query.agent-state", {{"pane_id", 999}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kNotFound);
}
