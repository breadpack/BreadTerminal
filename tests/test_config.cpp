#include "termcore/config.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#if !defined(_WIN32)
#include <sys/stat.h>
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
#if defined(_WIN32)
    // $HOME may not be set on Windows CI
    if (path.empty()) GTEST_SKIP() << "HOME not set on Windows";
#endif
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

TEST(ConfigTest, ParseBackgroundOpacity) {
    Config config = parseConfigString("background-opacity = 0.5");
    EXPECT_FLOAT_EQ(config.background_opacity, 0.5f);
}

TEST(ConfigTest, ParseBackgroundOpacityClampedLow) {
    Config config = parseConfigString("background-opacity = -0.5");
    EXPECT_FLOAT_EQ(config.background_opacity, 0.0f);
}

TEST(ConfigTest, ParseBackgroundOpacityClampedHigh) {
    Config config = parseConfigString("background-opacity = 2.0");
    EXPECT_FLOAT_EQ(config.background_opacity, 1.0f);
}

TEST(ConfigTest, ParseBackgroundBlur) {
    Config config = parseConfigString("background-blur = 2");
    EXPECT_EQ(config.background_blur, 2);
}

TEST(ConfigTest, DefaultBackgroundOpacityAndBlur) {
    Config config = parseConfigString("");
    EXPECT_FLOAT_EQ(config.background_opacity, 1.0f);
    EXPECT_EQ(config.background_blur, 0);
}

TEST(ConfigTest, SerializeDefaultConfig) {
    Config original;
    std::string text = serializeConfig(original);
    Config parsed = parseConfigString(text);

    EXPECT_EQ(parsed.font_family, original.font_family);
    EXPECT_FLOAT_EQ(parsed.font_size, original.font_size);
    EXPECT_EQ(parsed.background, original.background);
    EXPECT_EQ(parsed.foreground, original.foreground);
    EXPECT_EQ(parsed.cursor_color, original.cursor_color);
    EXPECT_EQ(parsed.selection_background, original.selection_background);
    EXPECT_EQ(parsed.selection_foreground, original.selection_foreground);
    EXPECT_EQ(parsed.window_width, original.window_width);
    EXPECT_EQ(parsed.window_height, original.window_height);
    EXPECT_EQ(parsed.scrollback_limit, original.scrollback_limit);
    EXPECT_EQ(parsed.cursor_style, original.cursor_style);
    EXPECT_EQ(parsed.cursor_blink, original.cursor_blink);
    EXPECT_FLOAT_EQ(parsed.background_opacity, original.background_opacity);
    EXPECT_EQ(parsed.background_blur, original.background_blur);
    EXPECT_EQ(parsed.sidebar_visible, original.sidebar_visible);
    EXPECT_EQ(parsed.sidebar_width, original.sidebar_width);
}

TEST(ConfigTest, SerializeCustomConfig) {
    Config original;
    original.font_family = "Fira Code";
    original.font_size = 18.0f;
    original.background = 0x000000;
    original.foreground = 0xffffff;
    original.cursor_color = 0xaabbcc;
    original.window_width = 1024;
    original.window_height = 768;
    original.scrollback_limit = 50000;
    original.cursor_style = "underline";
    original.cursor_blink = false;
    original.shell = "/bin/zsh";
    original.theme = "Dracula";
    original.background_opacity = 0.8f;
    original.background_blur = 2;
    original.sidebar_visible = false;
    original.sidebar_width = 300;
    original.clipboard_paste_protection = "always";
    original.clipboard_paste_bracketed_safe = false;
    original.font_features.push_back("calt");
    original.font_features.push_back("liga");
    original.keybindings.push_back({"cmd+t", "new_tab"});
    original.keybindings.push_back({"cmd+w", "close_tab"});

    std::string text = serializeConfig(original);
    Config parsed = parseConfigString(text);

    EXPECT_EQ(parsed.font_family, "Fira Code");
    EXPECT_FLOAT_EQ(parsed.font_size, 18.0f);
    EXPECT_EQ(parsed.background, 0x000000u);
    EXPECT_EQ(parsed.foreground, 0xffffffu);
    EXPECT_EQ(parsed.cursor_color, 0xaabbccu);
    EXPECT_EQ(parsed.window_width, 1024);
    EXPECT_EQ(parsed.window_height, 768);
    EXPECT_EQ(parsed.scrollback_limit, 50000);
    EXPECT_EQ(parsed.cursor_style, "underline");
    EXPECT_FALSE(parsed.cursor_blink);
    EXPECT_EQ(parsed.shell, "/bin/zsh");
    EXPECT_EQ(parsed.theme, "Dracula");
    EXPECT_FLOAT_EQ(parsed.background_opacity, 0.8f);
    EXPECT_EQ(parsed.background_blur, 2);
    EXPECT_FALSE(parsed.sidebar_visible);
    EXPECT_EQ(parsed.sidebar_width, 300);
    EXPECT_EQ(parsed.clipboard_paste_protection, "always");
    EXPECT_FALSE(parsed.clipboard_paste_bracketed_safe);
    ASSERT_EQ(parsed.font_features.size(), 2u);
    EXPECT_EQ(parsed.font_features[0], "calt");
    EXPECT_EQ(parsed.font_features[1], "liga");
    ASSERT_EQ(parsed.keybindings.size(), 2u);
    EXPECT_EQ(parsed.keybindings[0].trigger, "cmd+t");
    EXPECT_EQ(parsed.keybindings[0].action, "new_tab");
}

TEST(ConfigTest, SerializeColorsAsHex) {
    Config config;
    config.background = 0xff00ff;
    config.foreground = 0x123456;
    config.cursor_color = 0xaabbcc;

    std::string text = serializeConfig(config);

    EXPECT_NE(text.find("background = #ff00ff"), std::string::npos);
    EXPECT_NE(text.find("foreground = #123456"), std::string::npos);
    EXPECT_NE(text.find("cursor-color = #aabbcc"), std::string::npos);
}

TEST(ConfigTest, SerializePalette) {
    Config config;
    // Set distinct values for all 16 palette entries
    for (int i = 0; i < 16; ++i) {
        config.palette[i] = 0x100000 + static_cast<uint32_t>(i);
    }

    std::string text = serializeConfig(config);
    Config parsed = parseConfigString(text);

    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(parsed.palette[i], config.palette[i])
            << "Palette entry " << i << " mismatch";
    }

    // Verify the format includes "palette = N=#RRGGBB"
    EXPECT_NE(text.find("palette = 0=#100000"), std::string::npos);
    EXPECT_NE(text.find("palette = 15=#10000f"), std::string::npos);
}

TEST(ConfigTest, SerializeKeybindings) {
    Config config;
    config.keybindings.push_back({"cmd+t", "new_tab"});
    config.keybindings.push_back({"ctrl+shift+c", "copy"});
    config.keybindings.push_back({"cmd+d", "split_right"});

    std::string text = serializeConfig(config);

    EXPECT_NE(text.find("keybind = cmd+t=new_tab"), std::string::npos);
    EXPECT_NE(text.find("keybind = ctrl+shift+c=copy"), std::string::npos);
    EXPECT_NE(text.find("keybind = cmd+d=split_right"), std::string::npos);

    // Round-trip check
    Config parsed = parseConfigString(text);
    ASSERT_EQ(parsed.keybindings.size(), 3u);
    EXPECT_EQ(parsed.keybindings[0].trigger, "cmd+t");
    EXPECT_EQ(parsed.keybindings[0].action, "new_tab");
    EXPECT_EQ(parsed.keybindings[1].trigger, "ctrl+shift+c");
    EXPECT_EQ(parsed.keybindings[1].action, "copy");
    EXPECT_EQ(parsed.keybindings[2].trigger, "cmd+d");
    EXPECT_EQ(parsed.keybindings[2].action, "split_right");
}

TEST(ConfigTest, WriteConfigFileAtomicRoundTrip) {
    std::string path = std::tmpnam(nullptr);
    if (path.empty()) GTEST_SKIP() << "tmpnam failed";

    Config original;
    original.font_family = "Monaco";
    original.font_size = 16.0f;
    original.keybindings.push_back({"cmd+t", "new_tab"});

    ASSERT_TRUE(writeConfigFile(path, original));

    Config parsed = parseConfigFile(path);
    EXPECT_EQ(parsed.font_family, "Monaco");
    EXPECT_FLOAT_EQ(parsed.font_size, 16.0f);
    ASSERT_EQ(parsed.keybindings.size(), 1u);
    EXPECT_EQ(parsed.keybindings[0].trigger, "cmd+t");

    // Verify permissions (0600)
#if !defined(_WIN32)
    struct stat st;
    ASSERT_EQ(stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
#endif

    std::remove(path.c_str());
}
