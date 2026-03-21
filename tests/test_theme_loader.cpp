#include "termcore/theme_loader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

const char* kGhosttyTheme = R"(
# Dracula theme - Ghostty format
background = 282a36
foreground = f8f8f2
cursor-color = f8f8f2
selection-background = 44475a
selection-foreground = f8f8f2
palette = 0=#21222c
palette = 1=#ff5555
palette = 2=#50fa7b
palette = 3=#f1fa8c
palette = 4=#bd93f9
palette = 5=#ff79c6
palette = 6=#8be9fd
palette = 7=#f8f8f2
palette = 8=#6272a4
palette = 9=#ff6e6e
palette = 10=#69ff94
palette = 11=#ffffa5
palette = 12=#d6acff
palette = 13=#ff92df
palette = 14=#a4ffff
palette = 15=#ffffff
)";

const char* kKittyTheme = R"(
# Dracula theme - Kitty format
background #282a36
foreground #f8f8f2
cursor #f8f8f2
cursor_text_color #282a36
selection_background #44475a
selection_foreground #f8f8f2
color0 #21222c
color1 #ff5555
color2 #50fa7b
color3 #f1fa8c
color4 #bd93f9
color5 #ff79c6
color6 #8be9fd
color7 #f8f8f2
color8 #6272a4
color9 #ff6e6e
color10 #69ff94
color11 #ffffa5
color12 #d6acff
color13 #ff92df
color14 #a4ffff
color15 #ffffff
)";

const char* kWindowsTerminalTheme = R"({
    "name": "Dracula",
    "background": "#282a36",
    "foreground": "#f8f8f2",
    "cursorColor": "#f8f8f2",
    "selectionBackground": "#44475a",
    "black": "#21222c",
    "red": "#ff5555",
    "green": "#50fa7b",
    "yellow": "#f1fa8c",
    "blue": "#bd93f9",
    "purple": "#ff79c6",
    "cyan": "#8be9fd",
    "white": "#f8f8f2",
    "brightBlack": "#6272a4",
    "brightRed": "#ff6e6e",
    "brightGreen": "#69ff94",
    "brightYellow": "#ffffa5",
    "brightBlue": "#d6acff",
    "brightPurple": "#ff92df",
    "brightCyan": "#a4ffff",
    "brightWhite": "#ffffff"
})";

void verifyDraculaColors(const termcore::Theme& theme) {
    EXPECT_EQ(theme.background, 0x282a36u);
    EXPECT_EQ(theme.foreground, 0xf8f8f2u);
    EXPECT_EQ(theme.cursor_color, 0xf8f8f2u);
    EXPECT_EQ(theme.selection_background, 0x44475au);
    EXPECT_EQ(theme.palette[0], 0x21222cu);
    EXPECT_EQ(theme.palette[1], 0xff5555u);
    EXPECT_EQ(theme.palette[2], 0x50fa7bu);
    EXPECT_EQ(theme.palette[3], 0xf1fa8cu);
    EXPECT_EQ(theme.palette[4], 0xbd93f9u);
    EXPECT_EQ(theme.palette[5], 0xff79c6u);
    EXPECT_EQ(theme.palette[6], 0x8be9fdu);
    EXPECT_EQ(theme.palette[7], 0xf8f8f2u);
    EXPECT_EQ(theme.palette[8], 0x6272a4u);
    EXPECT_EQ(theme.palette[9], 0xff6e6eu);
    EXPECT_EQ(theme.palette[10], 0x69ff94u);
    EXPECT_EQ(theme.palette[11], 0xffffa5u);
    EXPECT_EQ(theme.palette[12], 0xd6acffu);
    EXPECT_EQ(theme.palette[13], 0xff92dfu);
    EXPECT_EQ(theme.palette[14], 0xa4ffffu);
    EXPECT_EQ(theme.palette[15], 0xffffffu);
}

class TempDir {
public:
    TempDir() {
        auto tmpl = fs::temp_directory_path() / "bt_theme_test_XXXXXX";
        path_ = tmpl.string();
        // Create a unique temporary directory
        path_ = fs::temp_directory_path() / ("bt_theme_test_" +
            std::to_string(std::hash<std::string>{}(
                std::to_string(reinterpret_cast<uintptr_t>(this)) +
                std::to_string(std::chrono::steady_clock::now()
                    .time_since_epoch().count()))));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};

} // namespace

TEST(ThemeLoader, ParseGhosttyFormat) {
    auto theme = termcore::parseThemeString(
        kGhosttyTheme, "TestDracula", termcore::ThemeFormat::Ghostty);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "TestDracula");
    verifyDraculaColors(*theme);
}

TEST(ThemeLoader, ParseKittyFormat) {
    auto theme = termcore::parseThemeString(
        kKittyTheme, "TestDracula", termcore::ThemeFormat::Kitty);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "TestDracula");
    verifyDraculaColors(*theme);
}

TEST(ThemeLoader, ParseWindowsTerminalFormat) {
    auto theme = termcore::parseThemeString(
        kWindowsTerminalTheme, "Fallback", termcore::ThemeFormat::WindowsTerminal);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "Dracula");  // JSON name field takes precedence
    verifyDraculaColors(*theme);
}

TEST(ThemeLoader, AutoDetectGhostty) {
    auto theme = termcore::parseThemeString(
        kGhosttyTheme, "AutoGhostty", termcore::ThemeFormat::Auto);
    ASSERT_TRUE(theme.has_value());
    verifyDraculaColors(*theme);
}

TEST(ThemeLoader, AutoDetectKitty) {
    auto theme = termcore::parseThemeString(
        kKittyTheme, "AutoKitty", termcore::ThemeFormat::Auto);
    ASSERT_TRUE(theme.has_value());
    verifyDraculaColors(*theme);
}

TEST(ThemeLoader, AutoDetectJson) {
    auto theme = termcore::parseThemeString(
        kWindowsTerminalTheme, "AutoJson", termcore::ThemeFormat::Auto);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "Dracula");
    verifyDraculaColors(*theme);
}

TEST(ThemeLoader, ParseInvalidReturnsNullopt) {
    auto theme = termcore::parseThemeString(
        "this is not a valid theme file\nno colors here",
        "Invalid", termcore::ThemeFormat::Ghostty);
    EXPECT_FALSE(theme.has_value());
}

TEST(ThemeLoader, ScanEmptyDirReturnsEmpty) {
    TempDir dir;
    auto themes = termcore::scanThemeDirectory(dir.path());
    EXPECT_TRUE(themes.empty());
}

TEST(ThemeLoader, ScanNonExistentDirReturnsEmpty) {
    auto themes = termcore::scanThemeDirectory("/tmp/nonexistent_theme_dir_12345");
    EXPECT_TRUE(themes.empty());
}

TEST(ThemeLoader, LoadThemeFile) {
    TempDir dir;
    std::string filePath = dir.path() + "/MyTheme";
    {
        std::ofstream f(filePath);
        f << kGhosttyTheme;
    }

    auto theme = termcore::loadThemeFile(filePath);
    ASSERT_TRUE(theme.ok());
    EXPECT_EQ(theme.value().name, "MyTheme");
    verifyDraculaColors(theme.value());
}

TEST(ThemeLoader, LoadThemeFileJson) {
    TempDir dir;
    std::string filePath = dir.path() + "/Dracula.json";
    {
        std::ofstream f(filePath);
        f << kWindowsTerminalTheme;
    }

    auto theme = termcore::loadThemeFile(filePath);
    ASSERT_TRUE(theme.ok());
    EXPECT_EQ(theme.value().name, "Dracula");
    verifyDraculaColors(theme.value());
}

TEST(ThemeLoader, ScanDirectoryFindsThemes) {
    TempDir dir;
    // Write a Ghostty theme
    {
        std::ofstream f(dir.path() + "/MyGhostty");
        f << kGhosttyTheme;
    }
    // Write a JSON theme
    {
        std::ofstream f(dir.path() + "/MyJson.json");
        f << kWindowsTerminalTheme;
    }

    auto themes = termcore::scanThemeDirectory(dir.path());
    EXPECT_EQ(themes.size(), 2u);
}

TEST(ThemeLoader, AllAvailableThemesIncludesBuiltins) {
    auto themes = termcore::allAvailableThemes();
    // Should include at least the 6 built-in themes
    EXPECT_GE(themes.size(), 6u);

    // Verify known built-in themes are present
    bool foundDracula = false;
    bool foundNord = false;
    for (const auto& t : themes) {
        if (t.name == "Dracula") foundDracula = true;
        if (t.name == "Nord") foundNord = true;
    }
    EXPECT_TRUE(foundDracula);
    EXPECT_TRUE(foundNord);
}

TEST(ThemeLoader, FindThemeBuiltin) {
    auto theme = termcore::findTheme("Dracula");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "Dracula");
    EXPECT_EQ(theme->background, 0x282a36u);
}

TEST(ThemeLoader, FindThemeNotFound) {
    auto theme = termcore::findTheme("NonExistentTheme12345");
    EXPECT_FALSE(theme.has_value());
}

TEST(ThemeLoader, DefaultThemeDirNotEmpty) {
    auto dir = termcore::defaultThemeDir();
    EXPECT_FALSE(dir.empty());
}

TEST(ThemeLoader, ColorParsingWithHash) {
    // Ghostty format with '#' prefix on colors
    const char* content = R"(
background = #112233
foreground = #aabbcc
)";
    auto theme = termcore::parseThemeString(
        content, "HashTest", termcore::ThemeFormat::Ghostty);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->background, 0x112233u);
    EXPECT_EQ(theme->foreground, 0xaabbccu);
}

TEST(ThemeLoader, ColorParsingWithoutHash) {
    // Ghostty format without '#' prefix
    const char* content = R"(
background = 112233
foreground = aabbcc
)";
    auto theme = termcore::parseThemeString(
        content, "NoHashTest", termcore::ThemeFormat::Ghostty);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->background, 0x112233u);
    EXPECT_EQ(theme->foreground, 0xaabbccu);
}
