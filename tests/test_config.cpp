#include "termcore/config.h"
#include "termcore/lua_config.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>

using namespace termcore;

// --- Config struct defaults ---

TEST(ConfigTest, DefaultConfigValues) {
    Config config;
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
    EXPECT_FLOAT_EQ(config.background_opacity, 1.0f);
    EXPECT_FLOAT_EQ(config.background_blur, 0.5f);
    EXPECT_EQ(config.clipboard_paste_protection, "multiline");
    EXPECT_TRUE(config.clipboard_paste_bracketed_safe);
    EXPECT_FALSE(config.allow_clipboard_write);
    EXPECT_TRUE(config.notify_on_command_finish);
    EXPECT_FLOAT_EQ(config.notify_after_seconds, 5.0f);
    EXPECT_FALSE(config.sidebar_visible);
    EXPECT_EQ(config.sidebar_width, 220);
}

// --- Lua config loading ---

TEST(ConfigTest, LoadLuaStringFont) {
    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ font_family = \"JetBrains Mono\", font_size = 16.5 })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.font_family, "JetBrains Mono");
    EXPECT_FLOAT_EQ(config.font_size, 16.5f);
}

TEST(ConfigTest, LoadLuaStringColors) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ background = 0xff00ff, foreground = 0x123456 })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.background, 0xff00ffu);
    EXPECT_EQ(config.foreground, 0x123456u);
}

TEST(ConfigTest, LoadLuaStringPalette) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ palette = { "
        "0xaaaaaa, 0xbbbbbb, 0xcccccc, 0xdddddd, "
        "0xeeeeee, 0xffffff, 0x111111, 0x222222, "
        "0x333333, 0x444444, 0x555555, 0x666666, "
        "0x777777, 0x888888, 0x999999, 0xabcdef } })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.palette[0], 0xaaaaaau);
    EXPECT_EQ(config.palette[15], 0xabcdefu);
}

TEST(ConfigTest, LoadLuaStringKeybinding) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.keymap(\"cmd+t\", \"new_tab\")").ok());
    const Config& config = luaConfig();
    ASSERT_EQ(config.keybindings.size(), 1u);
    EXPECT_EQ(config.keybindings[0].trigger, "cmd+t");
    EXPECT_EQ(config.keybindings[0].action, "new_tab");
}

TEST(ConfigTest, LoadLuaStringMultipleKeybindings) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.keymap(\"cmd+t\", \"new_tab\")\n"
        "terminal.keymap(\"cmd+w\", \"close_tab\")\n"
        "terminal.keymap(\"cmd+d\", \"split_right\")").ok());
    const Config& config = luaConfig();
    ASSERT_EQ(config.keybindings.size(), 3u);
    EXPECT_EQ(config.keybindings[2].trigger, "cmd+d");
    EXPECT_EQ(config.keybindings[2].action, "split_right");
}

TEST(ConfigTest, LoadLuaStringCursorConfig) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ cursor_style = \"underline\", cursor_blink = false })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.cursor_style, "underline");
    EXPECT_FALSE(config.cursor_blink);
}

TEST(ConfigTest, LoadLuaStringShell) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ shell = \"/bin/zsh\" })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.shell, "/bin/zsh");
}

TEST(ConfigTest, LoadLuaStringScrollbackLimit) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ scrollback_limit = 50000 })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.scrollback_limit, 50000);
}

TEST(ConfigTest, LoadLuaStringBackgroundOpacity) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ background_opacity = 0.5 })").ok());
    const Config& config = luaConfig();
    EXPECT_FLOAT_EQ(config.background_opacity, 0.5f);
}

TEST(ConfigTest, LoadLuaStringBackgroundBlur) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ background_blur = 2 })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.background_blur, 2);
}

TEST(ConfigTest, LoadLuaStringClipboardConfig) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ "
        "clipboard_paste_protection = \"always\", "
        "clipboard_paste_bracketed_safe = false, "
        "allow_clipboard_write = true })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.clipboard_paste_protection, "always");
    EXPECT_FALSE(config.clipboard_paste_bracketed_safe);
    EXPECT_TRUE(config.allow_clipboard_write);
}

TEST(ConfigTest, LoadLuaStringSidebar) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ sidebar_visible = false, sidebar_width = 300 })").ok());
    const Config& config = luaConfig();
    EXPECT_FALSE(config.sidebar_visible);
    EXPECT_EQ(config.sidebar_width, 300);
}

TEST(ConfigTest, LoadLuaStringFontFeatures) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ font_features = { \"calt\", \"liga\", \"dlig\" } })").ok());
    const Config& config = luaConfig();
    ASSERT_EQ(config.font_features.size(), 3u);
    EXPECT_EQ(config.font_features[0], "calt");
    EXPECT_EQ(config.font_features[1], "liga");
    EXPECT_EQ(config.font_features[2], "dlig");
}

TEST(ConfigTest, LoadLuaStringMultipleConfigCalls) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ font_family = \"Monaco\" })\n"
        "terminal.config({ font_size = 18 })\n").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.font_family, "Monaco");
    EXPECT_FLOAT_EQ(config.font_size, 18.0f);
}

TEST(ConfigTest, LoadLuaStringWindowSize) {

    ASSERT_TRUE(loadConfigLuaString(
        "terminal.config({ window_width = 1024, window_height = 768 })").ok());
    const Config& config = luaConfig();
    EXPECT_EQ(config.window_width, 1024);
    EXPECT_EQ(config.window_height, 768);
}

// --- Lua config serialization round-trip ---

TEST(ConfigTest, SerializeLuaDefaultConfig) {

    Config original;
    std::string lua = serializeConfigLua(original);

    ASSERT_TRUE(loadConfigLuaString(lua).ok());
    const Config& parsed = luaConfig();

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

TEST(ConfigTest, SerializeLuaCustomConfig) {

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

    std::string lua = serializeConfigLua(original);
    ASSERT_TRUE(loadConfigLuaString(lua).ok());
    const Config& parsed = luaConfig();

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

// --- Lua config file I/O ---

TEST(ConfigTest, WriteAndLoadLuaConfigFile) {

    std::string path = std::tmpnam(nullptr);
    if (path.empty()) GTEST_SKIP() << "tmpnam failed";
    path += ".lua";

    Config original;
    original.font_family = "Monaco";
    original.font_size = 16.0f;
    original.keybindings.push_back({"cmd+t", "new_tab"});

    ASSERT_TRUE(writeConfigLua(path, original));
    ASSERT_TRUE(loadConfigLua(path).ok());

    const Config& parsed = luaConfig();
    EXPECT_EQ(parsed.font_family, "Monaco");
    EXPECT_FLOAT_EQ(parsed.font_size, 16.0f);
    ASSERT_EQ(parsed.keybindings.size(), 1u);
    EXPECT_EQ(parsed.keybindings[0].trigger, "cmd+t");

    std::remove(path.c_str());
}

// --- Theme tests ---

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
    if (path.empty()) GTEST_SKIP() << "HOME not set on Windows";
#endif
    EXPECT_FALSE(path.empty());
}

// --- Adaptive theme tests ---

TEST(ConfigTest, AdaptiveThemeParsing) {
    EXPECT_TRUE(isAdaptiveTheme("dark:Dracula,light:One Light"));
    EXPECT_FALSE(isAdaptiveTheme("Dracula"));

    auto result = parseAdaptiveTheme("dark:Dracula,light:One Light");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->dark_theme, "Dracula");
    EXPECT_EQ(result->light_theme, "One Light");
}

TEST(ConfigTest, ResolveThemeForAppearance) {
    EXPECT_EQ(resolveThemeForAppearance("dark:Dracula,light:One Light", true), "Dracula");
    EXPECT_EQ(resolveThemeForAppearance("dark:Dracula,light:One Light", false), "One Light");
    EXPECT_EQ(resolveThemeForAppearance("Dracula", true), "Dracula");
}
