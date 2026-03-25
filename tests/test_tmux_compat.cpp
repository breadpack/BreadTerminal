#include <gtest/gtest.h>
#include "tmux_compat.h"

using namespace bread;

// Helper to build argv from initializer list
static ParsedArgs runTmux(std::initializer_list<const char*> args_list) {
    std::vector<char*> argv;
    for (auto a : args_list) {
        argv.push_back(const_cast<char*>(a));
    }
    return parseTmuxArgs(static_cast<int>(argv.size()), argv.data());
}

TEST(TmuxCompatTest, SplitWindowHorizontal) {
    auto args = runTmux({"split-window", "-h"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.type, CommandType::RemoteRPC);
    EXPECT_EQ(args.method, "pane.split");
    EXPECT_EQ(args.params["direction"], "horizontal");
}

TEST(TmuxCompatTest, SplitWindowVertical) {
    auto args = runTmux({"split-window", "-v"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.method, "pane.split");
    EXPECT_EQ(args.params["direction"], "vertical");
}

TEST(TmuxCompatTest, SplitWindowDefault) {
    auto args = runTmux({"split-window"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.method, "pane.split");
    EXPECT_EQ(args.params["direction"], "vertical");
}

TEST(TmuxCompatTest, SendKeysWithTarget) {
    auto args = runTmux({"send-keys", "-t", "%3", "hello", "Enter"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.method, "pane.sendKeys");
    EXPECT_EQ(args.params["pane_id"], 3);
    EXPECT_EQ(args.params["keys"], "hello\n");
}

TEST(TmuxCompatTest, SendKeysSpecialKeys) {
    auto args = runTmux({"send-keys", "C-c"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.params["keys"], "\x03");
}

TEST(TmuxCompatTest, SendKeysCtrlD) {
    auto args = runTmux({"send-keys", "C-d"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.params["keys"], "\x04");
}

TEST(TmuxCompatTest, SendKeysEscape) {
    auto args = runTmux({"send-keys", "Escape"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.params["keys"], "\x1b");
}

TEST(TmuxCompatTest, SendKeysSpaceAndTab) {
    auto args = runTmux({"send-keys", "Space", "Tab"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.params["keys"], " \t");
}

TEST(TmuxCompatTest, SelectPane) {
    auto args = runTmux({"select-pane", "-t", "%5"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.method, "pane.focus");
    EXPECT_EQ(args.params["pane_id"], 5);
}

TEST(TmuxCompatTest, ListPanes) {
    auto args = runTmux({"list-panes"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.method, "pane.list");
    EXPECT_TRUE(args.params.is_object());
}

TEST(TmuxCompatTest, KillPane) {
    auto args = runTmux({"kill-pane", "-t", "%2"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.method, "pane.close");
    EXPECT_EQ(args.params["pane_id"], 2);
}

TEST(TmuxCompatTest, DisplayMessage) {
    auto args = runTmux({"display-message", "-p", "#{pane_id}"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.method, "query.activePane");
}

TEST(TmuxCompatTest, UnsupportedCommand) {
    auto args = runTmux({"resize-pane", "-D", "5"});
    EXPECT_FALSE(args.valid);
    EXPECT_TRUE(args.error.find("Supported") != std::string::npos);
    EXPECT_TRUE(args.error.find("resize-pane") != std::string::npos);
}

TEST(TmuxCompatTest, NoCommand) {
    auto args = parseTmuxArgs(0, nullptr);
    EXPECT_FALSE(args.valid);
    EXPECT_TRUE(args.error.find("Supported") != std::string::npos);
}

TEST(TmuxCompatTest, TargetWithoutPercent) {
    auto args = runTmux({"select-pane", "-t", "7"});
    EXPECT_TRUE(args.valid);
    EXPECT_EQ(args.params["pane_id"], 7);
}
