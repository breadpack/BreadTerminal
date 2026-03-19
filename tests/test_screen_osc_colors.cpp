#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/dynamic_colors.h"
#include "termcore/vt_parser.h"
#include <string>
#include <vector>

namespace termcore {
namespace {

class OscColorTest : public ::testing::Test {
protected:
    Screen screen{24, 80};
    VtParser parser{screen};
    std::string last_response;
    std::vector<Screen::DynamicColorEvent> color_events;

    void SetUp() override {
        screen.setResponseCallback([this](const std::string& r) {
            last_response = r;
        });
        screen.setDynamicColorCallback([this](const Screen::DynamicColorEvent& e) {
            color_events.push_back(e);
        });
    }

    void feed(const std::string& data) {
        parser.feed(data.data(), data.size());
    }
};

// ---------------------------------------------------------------------------
// DynamicColors struct unit tests
// ---------------------------------------------------------------------------

TEST(DynamicColorsTest, DefaultPaletteXterm256) {
    DynamicColors dc;
    dc.resetAllPalette();
    // Check first 16 (xterm base)
    EXPECT_EQ(dc.palette[0], 0x000000u);
    EXPECT_EQ(dc.palette[1], 0xAA0000u);
    EXPECT_EQ(dc.palette[7], 0xAAAAAAu);
    EXPECT_EQ(dc.palette[8], 0x555555u);
    EXPECT_EQ(dc.palette[15], 0xFFFFFFu);

    // Color cube: index 16 = (0,0,0) = black
    EXPECT_EQ(dc.palette[16], 0x000000u);
    // index 21 = (0,0,5) = 0x0000FF
    EXPECT_EQ(dc.palette[21], 0x0000FFu);
    // index 196 = (5,0,0) = 0xFF0000
    EXPECT_EQ(dc.palette[196], 0xFF0000u);

    // Grayscale: 232 = gray 8, 255 = gray 238
    EXPECT_EQ(dc.palette[232], 0x080808u);
    EXPECT_EQ(dc.palette[255], 0xEEEEEEu);
}

TEST(DynamicColorsTest, ResolveFgBg) {
    DynamicColors dc;
    dc.foreground = 0xAABBCC;
    dc.background = 0x112233;

    EXPECT_EQ(dc.resolveFg(kColorDefault), 0xAABBCCu);
    EXPECT_EQ(dc.resolveFg(0xFF0000), 0xFF0000u);
    EXPECT_EQ(dc.resolveBg(kColorDefault), 0x112233u);
    EXPECT_EQ(dc.resolveBg(0x00FF00), 0x00FF00u);
}

TEST(DynamicColorsTest, ResetDynamic) {
    DynamicColors dc;
    dc.foreground = 0x123456;
    dc.resetDynamic(0);  // reset foreground
    EXPECT_EQ(dc.foreground, 0xFFFFFFu);

    dc.background = 0xABCDEF;
    dc.resetDynamic(1);  // reset background
    EXPECT_EQ(dc.background, 0x000000u);
}

TEST(DynamicColorsTest, ResetPaletteEntry) {
    DynamicColors dc;
    dc.resetAllPalette();
    dc.palette[0] = 0xFFFFFF;
    dc.resetPaletteEntry(0);
    EXPECT_EQ(dc.palette[0], 0x000000u);

    dc.palette[232] = 0xFFFFFF;
    dc.resetPaletteEntry(232);
    EXPECT_EQ(dc.palette[232], 0x080808u);
}

// ---------------------------------------------------------------------------
// OSC 4: Set/query palette color
// ---------------------------------------------------------------------------

TEST_F(OscColorTest, Osc4SetPaletteRgbColon) {
    feed("\033]4;1;rgb:ff/00/00\007");
    EXPECT_EQ(screen.dynamicColors().palette[1], 0xFF0000u);
    EXPECT_FALSE(color_events.empty());
}

TEST_F(OscColorTest, Osc4SetPalette16Bit) {
    feed("\033]4;2;rgb:ffff/0000/ffff\007");
    EXPECT_EQ(screen.dynamicColors().palette[2], 0xFF00FFu);
}

TEST_F(OscColorTest, Osc4SetPaletteHash) {
    feed("\033]4;5;#00ff00\007");
    EXPECT_EQ(screen.dynamicColors().palette[5], 0x00FF00u);
}

TEST_F(OscColorTest, Osc4QueryPalette) {
    // Set a known color first
    feed("\033]4;3;rgb:aa/bb/cc\007");
    last_response.clear();
    feed("\033]4;3;?\007");
    // Response: ESC ] 4 ; 3 ; rgb:aaaa/bbbb/cccc ESC backslash
    EXPECT_NE(last_response.find("4;3;rgb:aaaa/bbbb/cccc"), std::string::npos);
}

TEST_F(OscColorTest, Osc4MultiplePairs) {
    feed("\033]4;10;rgb:11/22/33;20;rgb:44/55/66\007");
    EXPECT_EQ(screen.dynamicColors().palette[10], 0x112233u);
    EXPECT_EQ(screen.dynamicColors().palette[20], 0x445566u);
}

// ---------------------------------------------------------------------------
// OSC 10-12: foreground, background, cursor
// ---------------------------------------------------------------------------

TEST_F(OscColorTest, Osc10SetForeground) {
    feed("\033]10;rgb:ff/80/00\007");
    EXPECT_EQ(screen.dynamicColors().foreground, 0xFF8000u);
}

TEST_F(OscColorTest, Osc11SetBackground) {
    feed("\033]11;rgb:00/00/80\007");
    EXPECT_EQ(screen.dynamicColors().background, 0x000080u);
}

TEST_F(OscColorTest, Osc12SetCursorColor) {
    feed("\033]12;#ff00ff\007");
    EXPECT_EQ(screen.dynamicColors().cursor_color, 0xFF00FFu);
}

TEST_F(OscColorTest, Osc10QueryForeground) {
    feed("\033]10;rgb:ab/cd/ef\007");
    last_response.clear();
    feed("\033]10;?\007");
    EXPECT_NE(last_response.find("10;rgb:abab/cdcd/efef"), std::string::npos);
}

TEST_F(OscColorTest, Osc10ChainedSetFgBgCursor) {
    feed("\033]10;rgb:11/22/33;rgb:44/55/66;rgb:77/88/99\007");
    EXPECT_EQ(screen.dynamicColors().foreground, 0x112233u);
    EXPECT_EQ(screen.dynamicColors().background, 0x445566u);
    EXPECT_EQ(screen.dynamicColors().cursor_color, 0x778899u);
}

// ---------------------------------------------------------------------------
// OSC 104: Reset palette
// ---------------------------------------------------------------------------

TEST_F(OscColorTest, Osc104ResetAll) {
    feed("\033]4;0;rgb:ff/ff/ff\007");
    EXPECT_EQ(screen.dynamicColors().palette[0], 0xFFFFFFu);
    feed("\033]104\007");
    EXPECT_EQ(screen.dynamicColors().palette[0], 0x000000u);
}

TEST_F(OscColorTest, Osc104ResetSpecific) {
    feed("\033]4;1;rgb:00/00/00\007");
    feed("\033]4;2;rgb:00/00/00\007");
    feed("\033]104;1\007");
    EXPECT_EQ(screen.dynamicColors().palette[1], 0xAA0000u);  // xterm default
    EXPECT_EQ(screen.dynamicColors().palette[2], 0x000000u);  // still modified
}

// ---------------------------------------------------------------------------
// OSC 110-119: Reset dynamic colors
// ---------------------------------------------------------------------------

TEST_F(OscColorTest, Osc110ResetForeground) {
    feed("\033]10;rgb:12/34/56\007");
    EXPECT_EQ(screen.dynamicColors().foreground, 0x123456u);
    feed("\033]110\007");
    EXPECT_EQ(screen.dynamicColors().foreground, 0xFFFFFFu);
}

TEST_F(OscColorTest, Osc111ResetBackground) {
    feed("\033]11;rgb:ab/cd/ef\007");
    feed("\033]111\007");
    EXPECT_EQ(screen.dynamicColors().background, 0x000000u);
}

// ---------------------------------------------------------------------------
// SGR + kColorDefault sentinel
// ---------------------------------------------------------------------------

TEST_F(OscColorTest, SgrDefaultFgUsesSentinel) {
    // After SGR 39, fg should be kColorDefault
    feed("\033[31m");   // set red
    feed("A");
    feed("\033[39m");   // reset fg
    feed("B");
    // Cell for 'A' should have palette red, cell for 'B' should have kColorDefault
    const auto& cellA = screen.cellAt(0, 0);
    const auto& cellB = screen.cellAt(0, 1);
    EXPECT_NE(cellA.fg_color, kColorDefault);
    EXPECT_EQ(cellB.fg_color, kColorDefault);
}

TEST_F(OscColorTest, SgrDefaultBgUsesSentinel) {
    feed("\033[41m");   // set red bg
    feed("A");
    feed("\033[49m");   // reset bg
    feed("B");
    const auto& cellA = screen.cellAt(0, 0);
    const auto& cellB = screen.cellAt(0, 1);
    EXPECT_NE(cellA.bg_color, kColorDefault);
    EXPECT_EQ(cellB.bg_color, kColorDefault);
}

TEST_F(OscColorTest, SgrPaletteColorFromDynamic) {
    // Change palette[1] (red) to blue
    feed("\033]4;1;rgb:00/00/ff\007");
    feed("\033[31mX");  // SGR 31 = fg color index 1
    const auto& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.fg_color, 0x0000FFu);
}

TEST_F(OscColorTest, Sgr256ColorFromDynamic) {
    // Change palette[100] to a custom color
    feed("\033]4;100;rgb:ab/cd/ef\007");
    feed("\033[38;5;100mY");
    const auto& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.fg_color, 0xABCDEFu);
}

// ---------------------------------------------------------------------------
// Callback tests
// ---------------------------------------------------------------------------

TEST_F(OscColorTest, DynamicColorCallbackFired) {
    color_events.clear();
    feed("\033]10;rgb:ff/00/00\007");
    ASSERT_EQ(color_events.size(), 1u);
    EXPECT_EQ(color_events[0].index, 0);  // slot 0 = foreground
    EXPECT_EQ(color_events[0].color, 0xFF0000u);
}

TEST_F(OscColorTest, PaletteCallbackFired) {
    color_events.clear();
    feed("\033]4;5;rgb:00/ff/00\007");
    ASSERT_EQ(color_events.size(), 1u);
    EXPECT_EQ(color_events[0].index, -1);  // palette
}

} // namespace
} // namespace termcore
