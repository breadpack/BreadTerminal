#include "termcore/config.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

using namespace termcore;

TEST(ConfigTest, ParseEmptyStringReturnsDefaults) {
    Config config = parseConfigString("");
    EXPECT_EQ(config.font_family, "Menlo");
    EXPECT_FLOAT_EQ(config.font_size, 14.0f);
    EXPECT_EQ(config.background, 0x1e1e2eu);
    EXPECT_EQ(config.foreground, 0xcdd6f4u);
    EXPECT_EQ(config.window_width, 800);
    EXPECT_EQ(config.window_height, 600);
    EXPECT_EQ(config.scrollback_limit, 10000);
    EXPECT_EQ(config.cursor_style, "block");
    EXPECT_TRUE(config.cursor_blink);
    EXPECT_TRUE(config.shell.empty());
    EXPECT_TRUE(config.keybindings.empty());
    EXPECT_TRUE(config.font_features.empty());
}

TEST(ConfigTest, ParseFontFamily) {
    Config config = parseConfigString("font-family = JetBrains Mono");
    EXPECT_EQ(config.font_family, "JetBrains Mono");
}

TEST(ConfigTest, ParseFontSize) {
    Config config = parseConfigString("font-size = 16.5");
    EXPECT_FLOAT_EQ(config.font_size, 16.5f);
}

TEST(ConfigTest, ParseHexColor) {
    Config config = parseConfigString("background = #ff00ff");
    EXPECT_EQ(config.background, 0xff00ffu);
}

TEST(ConfigTest, ParsePaletteEntry) {
    Config config = parseConfigString("palette = 3=#abcdef");
    EXPECT_EQ(config.palette[3], 0xabcdefu);
}

TEST(ConfigTest, ParseKeybinding) {
    Config config = parseConfigString("keybind = cmd+t=new_tab");
    ASSERT_EQ(config.keybindings.size(), 1u);
    EXPECT_EQ(config.keybindings[0].trigger, "cmd+t");
    EXPECT_EQ(config.keybindings[0].action, "new_tab");
}

TEST(ConfigTest, ParseCommentsAndEmptyLines) {
    Config config = parseConfigString(
        "# This is a comment\n"
        "\n"
        "   \n"
        "# Another comment\n"
        "font-family = Fira Code\n"
        "# trailing comment\n");
    EXPECT_EQ(config.font_family, "Fira Code");
}

TEST(ConfigTest, ParseUnknownKeysStoredInRaw) {
    Config config = parseConfigString("my-custom-key = some-value");
    ASSERT_EQ(config.raw.count("my-custom-key"), 1u);
    EXPECT_EQ(config.raw["my-custom-key"], "some-value");
}

TEST(ConfigTest, ParseMultiValueFontFeatures) {
    Config config = parseConfigString(
        "font-feature = calt\n"
        "font-feature = liga\n"
        "font-feature = dlig\n");
    ASSERT_EQ(config.font_features.size(), 3u);
    EXPECT_EQ(config.font_features[0], "calt");
    EXPECT_EQ(config.font_features[1], "liga");
    EXPECT_EQ(config.font_features[2], "dlig");
}

TEST(ConfigTest, ParseConfigFileWithTempFile) {
    std::string path = std::tmpnam(nullptr);
    if (path.empty()) GTEST_SKIP() << "tmpnam failed";
    {
        std::ofstream f(path);
        f << "font-family = Monaco\n"
          << "font-size = 12\n"
          << "background = #000000\n";
    }
    Config config = parseConfigFile(path);
    EXPECT_EQ(config.font_family, "Monaco");
    EXPECT_FLOAT_EQ(config.font_size, 12.0f);
    EXPECT_EQ(config.background, 0x000000u);
    std::remove(path.c_str());
}

TEST(ConfigTest, ParseConfigFileNonexistentReturnsDefault) {
    Config config = parseConfigFile("/nonexistent/path/to/config");
    EXPECT_EQ(config.font_family, "Menlo");
    EXPECT_FLOAT_EQ(config.font_size, 14.0f);
}

TEST(ConfigTest, GetBuiltinThemeDracula) {
    const Theme* theme = getBuiltinTheme("Dracula");
    ASSERT_NE(theme, nullptr);
    EXPECT_EQ(theme->name, "Dracula");
    EXPECT_EQ(theme->background, 0x282a36u);
}

TEST(ConfigTest, GetBuiltinThemeUnknownReturnsNull) {
    const Theme* theme = getBuiltinTheme("NonexistentTheme");
    EXPECT_EQ(theme, nullptr);
}

TEST(ConfigTest, ListBuiltinThemesNonEmpty) {
    auto themes = listBuiltinThemes();
    EXPECT_FALSE(themes.empty());
    EXPECT_GE(themes.size(), 6u);
}

TEST(ConfigTest, ApplyThemeChangesConfigColors) {
    Config config;
    const Theme* theme = getBuiltinTheme("Nord");
    ASSERT_NE(theme, nullptr);

    // Store originals
    uint32_t orig_bg = config.background;

    applyTheme(config, *theme);

    EXPECT_NE(config.background, orig_bg);
    EXPECT_EQ(config.background, theme->background);
    EXPECT_EQ(config.foreground, theme->foreground);
    EXPECT_EQ(config.cursor_color, theme->cursor_color);
    EXPECT_EQ(config.selection_background, theme->selection_background);
    EXPECT_EQ(config.selection_foreground, theme->selection_foreground);
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(config.palette[i], theme->palette[i]);
    }
}

TEST(ConfigTest, DefaultConfigPathNonEmpty) {
    std::string path = defaultConfigPath();
    EXPECT_FALSE(path.empty());
}

TEST(ConfigTest, ParseCursorBlink) {
    Config config = parseConfigString("cursor-blink = false");
    EXPECT_FALSE(config.cursor_blink);
}

TEST(ConfigTest, ParseMultipleKeybindings) {
    Config config = parseConfigString(
        "keybind = cmd+t=new_tab\n"
        "keybind = cmd+w=close_tab\n"
        "keybind = cmd+d=split_right\n");
    ASSERT_EQ(config.keybindings.size(), 3u);
    EXPECT_EQ(config.keybindings[2].trigger, "cmd+d");
    EXPECT_EQ(config.keybindings[2].action, "split_right");
}

TEST(ConfigTest, ParseTheme) {
    Config config = parseConfigString("theme = dark:Dracula,light:One Light");
    EXPECT_EQ(config.theme, "dark:Dracula,light:One Light");
}

TEST(ConfigTest, ParseShell) {
    Config config = parseConfigString("shell = /bin/zsh");
    EXPECT_EQ(config.shell, "/bin/zsh");
}

TEST(ConfigTest, ParseScrollbackLimit) {
    Config config = parseConfigString("scrollback-limit = 50000");
    EXPECT_EQ(config.scrollback_limit, 50000);
}

TEST(ConfigTest, ParseMultiplePaletteEntries) {
    Config config = parseConfigString(
        "palette = 0=#111111\n"
        "palette = 15=#eeeeee\n");
    EXPECT_EQ(config.palette[0], 0x111111u);
    EXPECT_EQ(config.palette[15], 0xeeeeeeu);
}
