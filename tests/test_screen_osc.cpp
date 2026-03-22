#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include <string>

namespace termcore {
namespace {

class ScreenOscTest : public ::testing::Test {
protected:
    Screen screen{24, 80};
    VtParser parser{screen};
    std::string last_response;
    std::vector<TermNotification> notifications;
    std::vector<Screen::ClipboardEvent> clipboard_events;

    void SetUp() override {
        screen.setResponseCallback([this](const std::string& r) {
            last_response = r;
        });
        screen.setNotificationCallback([this](const TermNotification& n) {
            notifications.push_back(n);
        });
        screen.setClipboardCallback([this](const Screen::ClipboardEvent& e) {
            clipboard_events.push_back(e);
        });
    }

    void feed(const std::string& data) {
        parser.feed(data.data(), data.size());
    }
};

// --- OSC 0: Set icon name and window title ---

TEST_F(ScreenOscTest, Osc0SetsTitle) {
    feed("\033]0;My Title\007");
    EXPECT_EQ(screen.title(), "My Title");
    EXPECT_EQ(screen.iconName(), "My Title");
}

// --- OSC 0 with ST (ESC \) terminator ---

TEST_F(ScreenOscTest, Osc0SetsTitleWithST) {
    feed("\033]0;My Title\033\\");
    EXPECT_EQ(screen.title(), "My Title");
    EXPECT_EQ(screen.iconName(), "My Title");
}

TEST_F(ScreenOscTest, Osc2SetsTitleWithST) {
    feed("\033]2;ST Title\033\\");
    EXPECT_EQ(screen.title(), "ST Title");
}

TEST_F(ScreenOscTest, Osc7WithST) {
    feed("\033]7;file:///home/user/projects\033\\");
    EXPECT_EQ(screen.workingDirectory(), "/home/user/projects");
}

TEST_F(ScreenOscTest, OscSTFollowedByNormalText) {
    feed("\033]2;My Terminal\033\\Hello");
    EXPECT_EQ(screen.title(), "My Terminal");
    EXPECT_EQ(screen.getLineText(0), "Hello");
}

TEST_F(ScreenOscTest, OscSTAndBelMixed) {
    feed("\033]0;First\033\\\033]2;Second\007");
    EXPECT_EQ(screen.title(), "Second");
    EXPECT_EQ(screen.iconName(), "First");
}

// --- OSC 1: Set icon name only ---

TEST_F(ScreenOscTest, Osc1SetsIconNameOnly) {
    // Set title first via OSC 0
    feed("\033]0;Initial\007");
    EXPECT_EQ(screen.title(), "Initial");
    EXPECT_EQ(screen.iconName(), "Initial");
    // Now set icon name only
    feed("\033]1;NewIcon\007");
    EXPECT_EQ(screen.iconName(), "NewIcon");
    // Title should remain unchanged
    EXPECT_EQ(screen.title(), "Initial");
}

// --- OSC 2: Set window title only ---

TEST_F(ScreenOscTest, Osc2SetsTitleOnly) {
    // Set both via OSC 0
    feed("\033]0;Initial\007");
    // Now set title only
    feed("\033]2;NewTitle\007");
    EXPECT_EQ(screen.title(), "NewTitle");
    // Icon name should remain unchanged
    EXPECT_EQ(screen.iconName(), "Initial");
}

// --- OSC 7: Working directory ---

TEST_F(ScreenOscTest, Osc7SetsWorkingDirectory) {
    feed("\033]7;file:///home/user/projects\007");
    EXPECT_EQ(screen.workingDirectory(), "/home/user/projects");
}

TEST_F(ScreenOscTest, Osc7WithHostname) {
    feed("\033]7;file://myhost/home/user/projects\007");
    EXPECT_EQ(screen.workingDirectory(), "/home/user/projects");
}

TEST_F(ScreenOscTest, Osc7UrlDecode) {
    feed("\033]7;file:///home/user/my%20folder\007");
    EXPECT_EQ(screen.workingDirectory(), "/home/user/my folder");
}

TEST_F(ScreenOscTest, Osc7NonFileUrl) {
    feed("\033]7;/some/path\007");
    EXPECT_EQ(screen.workingDirectory(), "/some/path");
}

// --- OSC 8: Hyperlink ---

TEST_F(ScreenOscTest, Osc8SetHyperlink) {
    feed("\033]8;;https://example.com\007");
    EXPECT_EQ(screen.currentHyperlink(), "https://example.com");
}

TEST_F(ScreenOscTest, Osc8ClearHyperlink) {
    feed("\033]8;;https://example.com\007");
    EXPECT_EQ(screen.currentHyperlink(), "https://example.com");
    feed("\033]8;;\007");
    EXPECT_EQ(screen.currentHyperlink(), "");
}

TEST_F(ScreenOscTest, Osc8WithParams) {
    feed("\033]8;id=link1;https://example.com\007");
    EXPECT_EQ(screen.currentHyperlink(), "https://example.com");
}

// --- OSC 9: Desktop notification ---

TEST_F(ScreenOscTest, Osc9Notification) {
    feed("\033]9;Hello World\007");
    ASSERT_EQ(notifications.size(), 1u);
    EXPECT_EQ(notifications[0].type, 9);
    EXPECT_EQ(notifications[0].body, "Hello World");
}

TEST_F(ScreenOscTest, Osc9LastNotification) {
    feed("\033]9;Test Message\007");
    EXPECT_EQ(screen.lastNotification().type, 9);
    EXPECT_EQ(screen.lastNotification().body, "Test Message");
}

// --- OSC 52: Clipboard ---

TEST_F(ScreenOscTest, Osc52ClipboardWrite) {
    screen.setClipboardWriteAllowed(true);
    feed("\033]52;c;SGVsbG8=\007");
    ASSERT_EQ(clipboard_events.size(), 1u);
    EXPECT_EQ(clipboard_events[0].selection, 'c');
    EXPECT_FALSE(clipboard_events[0].is_read);
    EXPECT_EQ(clipboard_events[0].data, "SGVsbG8=");
}

TEST_F(ScreenOscTest, Osc52ClipboardRead) {
    feed("\033]52;c;?\007");
    ASSERT_EQ(clipboard_events.size(), 1u);
    EXPECT_EQ(clipboard_events[0].selection, 'c');
    EXPECT_TRUE(clipboard_events[0].is_read);
}

TEST_F(ScreenOscTest, Osc52PrimarySelection) {
    screen.setClipboardWriteAllowed(true);
    feed("\033]52;p;SGVsbG8=\007");
    ASSERT_EQ(clipboard_events.size(), 1u);
    EXPECT_EQ(clipboard_events[0].selection, 'p');
}

// --- OSC 99: Kitty notification ---

TEST_F(ScreenOscTest, Osc99KittyNotification) {
    feed("\033]99;Kitty says hi\007");
    ASSERT_EQ(notifications.size(), 1u);
    EXPECT_EQ(notifications[0].type, 99);
    EXPECT_EQ(notifications[0].body, "Kitty says hi");
}

// --- OSC 133: Shell integration ---

TEST_F(ScreenOscTest, Osc133PromptStart) {
    feed("\033]133;A\007");
    EXPECT_EQ(screen.promptState(), PromptState::Prompt);
}

TEST_F(ScreenOscTest, Osc133InputStart) {
    feed("\033]133;A\007");
    feed("\033]133;B\007");
    EXPECT_EQ(screen.promptState(), PromptState::Input);
}

TEST_F(ScreenOscTest, Osc133OutputStart) {
    feed("\033]133;C\007");
    EXPECT_EQ(screen.promptState(), PromptState::Output);
}

TEST_F(ScreenOscTest, Osc133OutputEnd) {
    feed("\033]133;C\007");
    EXPECT_EQ(screen.promptState(), PromptState::Output);
    feed("\033]133;D\007");
    EXPECT_EQ(screen.promptState(), PromptState::None);
}

TEST_F(ScreenOscTest, Osc133FullCycle) {
    EXPECT_EQ(screen.promptState(), PromptState::None);
    feed("\033]133;A\007");
    EXPECT_EQ(screen.promptState(), PromptState::Prompt);
    feed("\033]133;B\007");
    EXPECT_EQ(screen.promptState(), PromptState::Input);
    feed("\033]133;C\007");
    EXPECT_EQ(screen.promptState(), PromptState::Output);
    feed("\033]133;D\007");
    EXPECT_EQ(screen.promptState(), PromptState::None);
}

// --- OSC 777: rxvt-unicode notification ---

TEST_F(ScreenOscTest, Osc777NotificationWithTitleAndBody) {
    feed("\033]777;notify;Alert;Something happened\007");
    ASSERT_EQ(notifications.size(), 1u);
    EXPECT_EQ(notifications[0].type, 777);
    EXPECT_EQ(notifications[0].title, "Alert");
    EXPECT_EQ(notifications[0].body, "Something happened");
}

TEST_F(ScreenOscTest, Osc777NotificationTitleOnly) {
    feed("\033]777;notify;Alert\007");
    ASSERT_EQ(notifications.size(), 1u);
    EXPECT_EQ(notifications[0].type, 777);
    EXPECT_EQ(notifications[0].title, "Alert");
    EXPECT_EQ(notifications[0].body, "");
}

// --- Edge cases ---

TEST_F(ScreenOscTest, EmptyOscString) {
    feed("\033]0;\007");
    EXPECT_EQ(screen.title(), "");
}

TEST_F(ScreenOscTest, UnknownOscNumber) {
    // Should not crash
    feed("\033]999;some data\007");
}

TEST_F(ScreenOscTest, MultipleOscSequences) {
    feed("\033]0;Title1\007\033]2;Title2\007\033]7;file:///tmp\007");
    EXPECT_EQ(screen.title(), "Title2");
    EXPECT_EQ(screen.iconName(), "Title1");
    EXPECT_EQ(screen.workingDirectory(), "/tmp");
}

TEST_F(ScreenOscTest, OscFollowedByNormalText) {
    feed("\033]2;My Terminal\007Hello");
    EXPECT_EQ(screen.title(), "My Terminal");
    EXPECT_EQ(screen.getLineText(0), "Hello");
}

TEST_F(ScreenOscTest, Osc133EmptyString) {
    // Should not crash on empty string
    feed("\033]133;\007");
    EXPECT_EQ(screen.promptState(), PromptState::None);
}

} // namespace
} // namespace termcore
