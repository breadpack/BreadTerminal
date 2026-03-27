#include "termcore/config_diff.h"

#include <gtest/gtest.h>

using namespace termcore;

TEST(ConfigDiff, NoChangeReturnsNone) {
    Config a;
    Config b;
    EXPECT_EQ(diffConfig(a, b), ConfigDirtyFlags::None);
}

TEST(ConfigDiff, BackgroundChangeReturnsColors) {
    Config a;
    Config b;
    b.background = 0x000000;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Colors));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Font));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::CursorStyle));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Keybindings));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Scrollback));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::WindowSize));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Theme));
}

TEST(ConfigDiff, FontFamilyChangeReturnsFont) {
    Config a;
    Config b;
    b.font_family = "Monaco";
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Font));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, FontSizeChangeReturnsFont) {
    Config a;
    Config b;
    b.font_size = 20.0f;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Font));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, PaletteChangeReturnsColors) {
    Config a;
    Config b;
    b.palette[5] = 0xFFFFFF;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Colors));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Font));
}

TEST(ConfigDiff, CursorStyleChangeReturnsCursorStyle) {
    Config a;
    Config b;
    b.cursor_style = "bar";
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::CursorStyle));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, CursorBlinkChangeReturnsCursorStyle) {
    Config a;
    Config b;
    b.cursor_blink = false;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::CursorStyle));
}

TEST(ConfigDiff, KeybindingsChangeReturnsKeybindings) {
    Config a;
    Config b;
    b.keybindings.push_back({"cmd+t", "new_tab"});
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Keybindings));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, KeybindingsSameSizeDifferentContent) {
    Config a;
    Config b;
    a.keybindings.push_back({"cmd+t", "new_tab"});
    b.keybindings.push_back({"cmd+t", "close_tab"});
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Keybindings));
}

TEST(ConfigDiff, ScrollbackChangeReturnsScrollback) {
    Config a;
    Config b;
    b.scrollback_limit = 5000;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Scrollback));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, WindowSizeChangeReturnsWindowSize) {
    Config a;
    Config b;
    b.window_width = 1024;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::WindowSize));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, ThemeChangeReturnsTheme) {
    Config a;
    Config b;
    b.theme = "solarized-dark";
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Theme));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, MultiGroupChangeReturnsMultipleFlags) {
    Config a;
    Config b;
    b.background = 0x000000;    // Colors
    b.font_size = 20.0f;        // Font
    b.scrollback_limit = 5000;  // Scrollback
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Colors));
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Font));
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Scrollback));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::CursorStyle));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Keybindings));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::WindowSize));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Theme));
}

TEST(ConfigDiff, ForegroundChangeReturnsColors) {
    Config a;
    Config b;
    b.foreground = 0x000000;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, SelectionColorsChangeReturnsColors) {
    Config a;
    Config b;
    b.selection_background = 0xAAAAAA;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, FontFeaturesChangeReturnsFont) {
    Config a;
    Config b;
    b.font_features.push_back("liga");
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Font));
}

TEST(ConfigDiff, WindowHeightChangeReturnsWindowSize) {
    Config a;
    Config b;
    b.window_height = 900;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::WindowSize));
}

TEST(ConfigDiff, OpacityChangeReturnsOpacity) {
    Config a;
    Config b;
    b.background_opacity = 0.5f;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Opacity));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, BlurChangeReturnsOpacity) {
    Config a;
    Config b;
    b.background_blur = 2;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Opacity));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, ClipboardProtectionChangeReturnsClipboard) {
    Config a;
    Config b;
    b.clipboard_paste_protection = "always";
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Clipboard));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, SidebarVisibleChangeReturnsSidebar) {
    Config a;
    Config b;
    b.sidebar_visible = true;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Sidebar));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}

TEST(ConfigDiff, SidebarWidthChangeReturnsSidebar) {
    Config a;
    Config b;
    b.sidebar_width = 300;
    auto flags = diffConfig(a, b);
    EXPECT_TRUE(hasFlag(flags, ConfigDirtyFlags::Sidebar));
    EXPECT_FALSE(hasFlag(flags, ConfigDirtyFlags::Colors));
}
