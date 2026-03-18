#include <gtest/gtest.h>
#include "termcore/mouse.h"
#include "termcore/screen.h"
#include "termcore/termcore.h"
#include <cstring>
#include <string>

using namespace termcore;

// Helper: build a MouseEvent
static MouseEvent makeEvent(MouseEventType type, MouseButton button,
                             int col, int row,
                             bool shift = false, bool alt = false,
                             bool ctrl = false) {
    MouseEvent e;
    e.type = type;
    e.button = button;
    e.col = col;
    e.row = row;
    e.shift = shift;
    e.alt = alt;
    e.ctrl = ctrl;
    return e;
}

// --- 1. No mouse mode → empty output ---
TEST(Mouse, NoModeReturnsEmpty) {
    auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 5, 10);
    auto seq = encodeMouseEvent(e, MouseMode::None, MouseEncoding::Default);
    EXPECT_TRUE(seq.empty());
}

// --- 2. X10 mode + left click → \033[M sequence ---
TEST(Mouse, X10LeftClick) {
    auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 0, 0);
    auto seq = encodeMouseEvent(e, MouseMode::X10, MouseEncoding::Default);
    ASSERT_EQ(seq.size(), 6u);
    EXPECT_EQ(seq[0], '\033');
    EXPECT_EQ(seq[1], '[');
    EXPECT_EQ(seq[2], 'M');
    // button=0 + 32 = 32
    EXPECT_EQ(static_cast<unsigned char>(seq[3]), 32u);
    // col=0 → 0+1+32=33
    EXPECT_EQ(static_cast<unsigned char>(seq[4]), 33u);
    // row=0 → 0+1+32=33
    EXPECT_EQ(static_cast<unsigned char>(seq[5]), 33u);
}

// --- 3. X10 mode + release → empty ---
TEST(Mouse, X10ReleaseEmpty) {
    auto e = makeEvent(MouseEventType::Release, MouseButton::Left, 5, 5);
    auto seq = encodeMouseEvent(e, MouseMode::X10, MouseEncoding::Default);
    EXPECT_TRUE(seq.empty());
}

// --- 4. ButtonEvent mode + release → produces sequence ---
TEST(Mouse, ButtonEventRelease) {
    auto e = makeEvent(MouseEventType::Release, MouseButton::Left, 10, 20);
    auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::Default);
    ASSERT_EQ(seq.size(), 6u);
    // Release button code = 3, + 32 = 35
    EXPECT_EQ(static_cast<unsigned char>(seq[3]), 35u);
}

// --- 5. AnyEvent mode + motion → produces sequence ---
TEST(Mouse, AnyEventMotion) {
    auto e = makeEvent(MouseEventType::Move, MouseButton::Left, 5, 5);
    auto seq = encodeMouseEvent(e, MouseMode::AnyEvent, MouseEncoding::Default);
    ASSERT_EQ(seq.size(), 6u);
    // button=0 | 32 (motion) = 32, + 32 = 64
    EXPECT_EQ(static_cast<unsigned char>(seq[3]), 64u);
}

// --- 6. SGR encoding + left press → \033[<0;col;rowM ---
TEST(Mouse, SGRLeftPress) {
    auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 9, 19);
    auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
    // \033[<0;10;20M
    EXPECT_EQ(seq, "\033[<0;10;20M");
}

// --- 7. SGR encoding + release → lowercase m ---
TEST(Mouse, SGRRelease) {
    auto e = makeEvent(MouseEventType::Release, MouseButton::Left, 9, 19);
    auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
    EXPECT_EQ(seq, "\033[<0;10;20m");
}

// --- 8. SGR preserves full coordinates (no 223 limit) ---
TEST(Mouse, SGRLargeCoordinates) {
    auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 299, 499);
    auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
    EXPECT_EQ(seq, "\033[<0;300;500M");
}

// --- 9. Default encoding coordinates offset correctly ---
TEST(Mouse, DefaultCoordinateOffset) {
    auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 10, 20);
    auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::Default);
    ASSERT_EQ(seq.size(), 6u);
    // col=10 → 10+1+32=43
    EXPECT_EQ(static_cast<unsigned char>(seq[4]), 43u);
    // row=20 → 20+1+32=53
    EXPECT_EQ(static_cast<unsigned char>(seq[5]), 53u);
}

// --- 10. Modifier keys in button byte ---
TEST(Mouse, ModifierKeys) {
    // Shift
    {
        auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 0, 0,
                           true, false, false);
        auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
        EXPECT_EQ(seq, "\033[<4;1;1M");  // button=0|4=4
    }
    // Alt
    {
        auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 0, 0,
                           false, true, false);
        auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
        EXPECT_EQ(seq, "\033[<8;1;1M");  // button=0|8=8
    }
    // Ctrl
    {
        auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 0, 0,
                           false, false, true);
        auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
        EXPECT_EQ(seq, "\033[<16;1;1M");  // button=0|16=16
    }
    // All modifiers
    {
        auto e = makeEvent(MouseEventType::Press, MouseButton::Left, 0, 0,
                           true, true, true);
        auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
        EXPECT_EQ(seq, "\033[<28;1;1M");  // 0|4|8|16=28
    }
}

// --- 11. Scroll events encode correctly ---
TEST(Mouse, ScrollEvents) {
    // ScrollUp - default encoding
    {
        auto e = makeEvent(MouseEventType::ScrollUp, MouseButton::ScrollUp, 5, 5);
        auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::Default);
        ASSERT_EQ(seq.size(), 6u);
        // button = 64+0 = 64, + 32 = 96
        EXPECT_EQ(static_cast<unsigned char>(seq[3]), 96u);
    }
    // ScrollDown - SGR encoding
    {
        auto e = makeEvent(MouseEventType::ScrollDown, MouseButton::ScrollDown, 5, 5);
        auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
        EXPECT_EQ(seq, "\033[<65;6;6M");  // 64+1=65
    }
}

// --- 12. ButtonEvent mode filters motion without button ---
TEST(Mouse, ButtonEventFiltersMoveWithoutButton) {
    auto e = makeEvent(MouseEventType::Move, MouseButton::Release, 5, 5);
    auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::Default);
    EXPECT_TRUE(seq.empty());
}

// --- 13. C API tc_pane_encode_mouse works ---
TEST(Mouse, CApiEncodeMouse) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    // Enable mouse mode via escape sequence: \033[?1000h (X10)
    const char* enable = "\033[?1000h";
    tc_pane_feed(pane, enable, strlen(enable));

    char buf[64] = {};
    // type=0 (Press), button=0 (Left), col=5, row=10, mods=0
    int len = tc_pane_encode_mouse(pane, 0, 0, 5, 10, 0, buf, sizeof(buf));
    ASSERT_GT(len, 0);
    std::string seq(buf, len);
    ASSERT_EQ(seq.size(), 6u);
    EXPECT_EQ(seq[0], '\033');
    EXPECT_EQ(seq[1], '[');
    EXPECT_EQ(seq[2], 'M');

    tc_pane_destroy(pane);
    tc_destroy(core);
}

// --- 14. C API with null pane returns 0 ---
TEST(Mouse, CApiNullPane) {
    char buf[64] = {};
    int len = tc_pane_encode_mouse(nullptr, 0, 0, 0, 0, 0, buf, sizeof(buf));
    EXPECT_EQ(len, 0);
}

// --- 15. SGR release preserves actual button (right click) ---
TEST(Mouse, SGRRightRelease) {
    auto e = makeEvent(MouseEventType::Release, MouseButton::Right, 3, 7);
    auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
    EXPECT_EQ(seq, "\033[<2;4;8m");
}

// --- 16. Middle button press ---
TEST(Mouse, MiddleButtonPress) {
    auto e = makeEvent(MouseEventType::Press, MouseButton::Middle, 0, 0);
    auto seq = encodeMouseEvent(e, MouseMode::ButtonEvent, MouseEncoding::SGR);
    EXPECT_EQ(seq, "\033[<1;1;1M");
}

// --- 17. AnyEvent mode reports motion even without button ---
TEST(Mouse, AnyEventMotionNoButton) {
    auto e = makeEvent(MouseEventType::Move, MouseButton::Release, 5, 5);
    auto seq = encodeMouseEvent(e, MouseMode::AnyEvent, MouseEncoding::Default);
    EXPECT_FALSE(seq.empty());
    ASSERT_EQ(seq.size(), 6u);
    // button=3 | 32(motion) = 35, + 32 = 67
    EXPECT_EQ(static_cast<unsigned char>(seq[3]), 67u);
}
