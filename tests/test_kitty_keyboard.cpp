#include <gtest/gtest.h>
#include "termcore/kitty_keyboard.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include <string>

namespace termcore {
namespace {

// ===== KittyKeyboardState Tests (1-5) =====

TEST(KittyKeyboardState, PushMode) {
    KittyKeyboardState state;
    EXPECT_FALSE(state.isActive());
    state.pushMode(KittyDisambiguate);
    EXPECT_TRUE(state.isActive());
    EXPECT_EQ(state.currentFlags(), KittyDisambiguate);
}

TEST(KittyKeyboardState, PopMode) {
    KittyKeyboardState state;
    state.pushMode(KittyDisambiguate);
    state.pushMode(KittyReportEvents);
    EXPECT_EQ(state.stackDepth(), 2u);
    state.popMode(1);
    EXPECT_EQ(state.stackDepth(), 1u);
    EXPECT_EQ(state.currentFlags(), KittyDisambiguate);
}

TEST(KittyKeyboardState, CurrentFlagsEmptyStack) {
    KittyKeyboardState state;
    EXPECT_EQ(state.currentFlags(), 0u);
}

TEST(KittyKeyboardState, Reset) {
    KittyKeyboardState state;
    state.pushMode(1);
    state.pushMode(2);
    state.pushMode(3);
    state.reset();
    EXPECT_FALSE(state.isActive());
    EXPECT_EQ(state.stackDepth(), 0u);
    EXPECT_EQ(state.currentFlags(), 0u);
}

TEST(KittyKeyboardState, StackDepth) {
    KittyKeyboardState state;
    EXPECT_EQ(state.stackDepth(), 0u);
    state.pushMode(1);
    EXPECT_EQ(state.stackDepth(), 1u);
    state.pushMode(2);
    EXPECT_EQ(state.stackDepth(), 2u);
    state.popMode(5); // pop more than available
    EXPECT_EQ(state.stackDepth(), 0u);
}

// ===== encodeKittyKey Tests (6-10) =====

TEST(EncodeKittyKey, BasicDisambiguate) {
    KittyKeyEvent event;
    event.key_code = 'a';
    event.modifiers = 0;
    std::string result = encodeKittyKey(event, KittyDisambiguate);
    EXPECT_EQ(result, "\033[97u");
}

TEST(EncodeKittyKey, WithModifiers) {
    KittyKeyEvent event;
    event.key_code = 'a';
    event.modifiers = 1; // shift
    std::string result = encodeKittyKey(event, KittyDisambiguate);
    // modifiers + 1 = 2
    EXPECT_EQ(result, "\033[97;2u");
}

TEST(EncodeKittyKey, WithEventType) {
    KittyKeyEvent event;
    event.key_code = 'a';
    event.modifiers = 0;
    event.event_type = KittyEventType::Release;
    std::string result = encodeKittyKey(event, KittyDisambiguate | KittyReportEvents);
    // modifiers+1=1, event_type=3
    EXPECT_EQ(result, "\033[97;1:3u");
}

TEST(EncodeKittyKey, SpecialKeys) {
    KittyKeyEvent event;
    event.key_code = KittyKey::Escape;
    event.modifiers = 0;
    std::string result = encodeKittyKey(event, KittyDisambiguate);
    EXPECT_EQ(result, "\033[27u");
}

TEST(EncodeKittyKey, NoFlagsReturnsEmpty) {
    KittyKeyEvent event;
    event.key_code = 'a';
    std::string result = encodeKittyKey(event, 0);
    EXPECT_EQ(result, "");
}

// ===== Screen CSI Integration Tests (11-13) =====

class KittyScreenTest : public ::testing::Test {
protected:
    Screen screen{24, 80};

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }
};

TEST_F(KittyScreenTest, CsiPushMode) {
    // CSI > 1 u  — push flags=1
    feed("\033[>1u");
    EXPECT_TRUE(screen.kittyKeyboard().isActive());
    EXPECT_EQ(screen.kittyKeyboard().currentFlags(), 1u);
}

TEST_F(KittyScreenTest, CsiPopMode) {
    feed("\033[>1u");
    feed("\033[>3u");
    EXPECT_EQ(screen.kittyKeyboard().stackDepth(), 2u);
    // CSI < 1 u  — pop 1
    feed("\033[<1u");
    EXPECT_EQ(screen.kittyKeyboard().stackDepth(), 1u);
    EXPECT_EQ(screen.kittyKeyboard().currentFlags(), 1u);
}

TEST_F(KittyScreenTest, CsiQueryMode) {
    feed("\033[>5u");
    std::string response;
    screen.setResponseCallback([&](const std::string& s) { response = s; });
    // CSI ? u  — query
    feed("\033[?u");
    EXPECT_EQ(response, "\033[?5u");
}

} // namespace
} // namespace termcore
