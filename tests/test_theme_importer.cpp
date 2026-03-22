#include "termcore/theme_importer.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = (fs::temp_directory_path() /
                 ("bt_importer_test_" +
                  std::to_string(std::hash<std::string>{}(
                      std::to_string(reinterpret_cast<uintptr_t>(this)) +
                      std::to_string(std::chrono::steady_clock::now()
                                         .time_since_epoch()
                                         .count())))))
                    .string();
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

// --- Ghostty format ---
const char* kGhosttyContent = R"(
# Dracula Ghostty theme
background = #282a36
foreground = #f8f8f2
cursor-color = #f8f8f2
selection-background = #44475a
selection-foreground = #f8f8f2
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

// --- iTerm2 format ---
const char* kITerm2Content = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Background Color</key>
    <dict>
        <key>Red Component</key>
        <real>0.15686274509803921</real>
        <key>Green Component</key>
        <real>0.16470588235294117</real>
        <key>Blue Component</key>
        <real>0.21176470588235294</real>
    </dict>
    <key>Foreground Color</key>
    <dict>
        <key>Red Component</key>
        <real>0.97254901960784312</real>
        <key>Green Component</key>
        <real>0.97254901960784312</real>
        <key>Blue Component</key>
        <real>0.94901960784313721</real>
    </dict>
    <key>Cursor Color</key>
    <dict>
        <key>Red Component</key>
        <real>0.97254901960784312</real>
        <key>Green Component</key>
        <real>0.97254901960784312</real>
        <key>Blue Component</key>
        <real>0.94901960784313721</real>
    </dict>
    <key>Ansi 0 Color</key>
    <dict>
        <key>Red Component</key>
        <real>0.12941176470588237</real>
        <key>Green Component</key>
        <real>0.13333333333333333</real>
        <key>Blue Component</key>
        <real>0.17254901960784313</real>
    </dict>
    <key>Ansi 1 Color</key>
    <dict>
        <key>Red Component</key>
        <real>1.0</real>
        <key>Green Component</key>
        <real>0.33333333333333331</real>
        <key>Blue Component</key>
        <real>0.33333333333333331</real>
    </dict>
</dict>
</plist>
)";

// --- Windows Terminal format ---
const char* kWindowsTerminalContent = R"({
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

// --- Alacritty TOML format ---
const char* kAlacrittyContent = R"(
[colors.primary]
background = "#282a36"
foreground = "#f8f8f2"

[colors.cursor]
cursor = "#f8f8f2"

[colors.selection]
background = "#44475a"
text = "#f8f8f2"

[colors.normal]
black = "#21222c"
red = "#ff5555"
green = "#50fa7b"
yellow = "#f1fa8c"
blue = "#bd93f9"
magenta = "#ff79c6"
cyan = "#8be9fd"
white = "#f8f8f2"

[colors.bright]
black = "#6272a4"
red = "#ff6e6e"
green = "#69ff94"
yellow = "#ffffa5"
blue = "#d6acff"
magenta = "#ff92df"
cyan = "#a4ffff"
white = "#ffffff"
)";

void verifyDraculaBasicColors(const termcore::Theme& theme) {
    EXPECT_EQ(theme.background, 0x282a36u);
    EXPECT_EQ(theme.foreground, 0xf8f8f2u);
}

void verifyDraculaPalette(const termcore::Theme& theme) {
    EXPECT_EQ(theme.palette[0], 0x21222cu);
    EXPECT_EQ(theme.palette[1], 0xff5555u);
    EXPECT_EQ(theme.palette[2], 0x50fa7bu);
    EXPECT_EQ(theme.palette[7], 0xf8f8f2u);
    EXPECT_EQ(theme.palette[15], 0xffffffu);
}

} // namespace

// --- Format detection tests ---

TEST(ThemeImporter, DetectGhosttyFormat) {
    auto fmt = termcore::detectImportFormat(kGhosttyContent, "theme");
    EXPECT_EQ(fmt, termcore::ThemeImportFormat::Ghostty);
}

TEST(ThemeImporter, DetectITerm2FormatByExtension) {
    auto fmt = termcore::detectImportFormat("", "Dracula.itermcolors");
    EXPECT_EQ(fmt, termcore::ThemeImportFormat::ITerm2);
}

TEST(ThemeImporter, DetectITerm2FormatByContent) {
    auto fmt = termcore::detectImportFormat(kITerm2Content, "theme");
    EXPECT_EQ(fmt, termcore::ThemeImportFormat::ITerm2);
}

TEST(ThemeImporter, DetectWindowsTerminalFormat) {
    auto fmt = termcore::detectImportFormat(kWindowsTerminalContent, "theme.json");
    EXPECT_EQ(fmt, termcore::ThemeImportFormat::WindowsTerminal);
}

TEST(ThemeImporter, DetectAlacrittyFormatByExtension) {
    auto fmt = termcore::detectImportFormat("", "theme.toml");
    EXPECT_EQ(fmt, termcore::ThemeImportFormat::Alacritty);
}

TEST(ThemeImporter, DetectAlacrittyFormatByContent) {
    auto fmt = termcore::detectImportFormat(kAlacrittyContent, "theme");
    EXPECT_EQ(fmt, termcore::ThemeImportFormat::Alacritty);
}

// --- Ghostty import ---

TEST(ThemeImporter, ImportFromGhostty) {
    auto theme = termcore::importFromGhostty(kGhosttyContent, "DraculaGhostty");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "DraculaGhostty");
    verifyDraculaBasicColors(*theme);
    verifyDraculaPalette(*theme);
    EXPECT_EQ(theme->cursor_color, 0xf8f8f2u);
    EXPECT_EQ(theme->selection_background, 0x44475au);
}

// --- iTerm2 import ---

TEST(ThemeImporter, ImportFromITerm2) {
    auto theme = termcore::importFromITerm2(kITerm2Content, "DraculaITerm");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "DraculaITerm");
    // iTerm2 uses float components, so there may be small rounding differences.
    // Background: R=0.157, G=0.165, B=0.212 -> ~(40, 42, 54) -> 0x282a36
    EXPECT_EQ(theme->background, 0x282a36u);
    // Foreground: R=0.973, G=0.973, B=0.949 -> ~(248, 248, 242) -> 0xf8f8f2
    EXPECT_EQ(theme->foreground, 0xf8f8f2u);
    // Ansi 0: palette[0]
    EXPECT_EQ(theme->palette[0], 0x21222cu);
    // Ansi 1: R=1.0, G=0.333, B=0.333 -> (255, 85, 85) -> 0xff5555
    EXPECT_EQ(theme->palette[1], 0xff5555u);
}

TEST(ThemeImporter, ImportFromITerm2InvalidReturnsNullopt) {
    auto theme = termcore::importFromITerm2("not xml at all", "Bad");
    EXPECT_FALSE(theme.has_value());
}

// --- Windows Terminal import ---

TEST(ThemeImporter, ImportFromWindowsTerminal) {
    auto theme =
        termcore::importFromWindowsTerminal(kWindowsTerminalContent, "Fallback");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "Dracula"); // JSON name takes precedence
    verifyDraculaBasicColors(*theme);
    verifyDraculaPalette(*theme);
}

// --- Alacritty import ---

TEST(ThemeImporter, ImportFromAlacritty) {
    auto theme = termcore::importFromAlacritty(kAlacrittyContent, "DraculaAlacritty");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "DraculaAlacritty");
    verifyDraculaBasicColors(*theme);
    EXPECT_EQ(theme->cursor_color, 0xf8f8f2u);
    EXPECT_EQ(theme->selection_background, 0x44475au);
    verifyDraculaPalette(*theme);
}

TEST(ThemeImporter, ImportFromAlacrittyInvalidReturnsNullopt) {
    auto theme = termcore::importFromAlacritty("nothing useful here", "Bad");
    EXPECT_FALSE(theme.has_value());
}

// --- File import with auto-detection ---

TEST(ThemeImporter, ImportFromFileGhostty) {
    TempDir dir;
    std::string path = dir.path() + "/MyTheme";
    {
        std::ofstream f(path);
        f << kGhosttyContent;
    }
    auto theme = termcore::importFromFile(path);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "MyTheme");
    verifyDraculaBasicColors(*theme);
}

TEST(ThemeImporter, ImportFromFileJson) {
    TempDir dir;
    std::string path = dir.path() + "/Dracula.json";
    {
        std::ofstream f(path);
        f << kWindowsTerminalContent;
    }
    auto theme = termcore::importFromFile(path);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "Dracula");
    verifyDraculaBasicColors(*theme);
}

TEST(ThemeImporter, ImportFromFileITerm2) {
    TempDir dir;
    std::string path = dir.path() + "/Dracula.itermcolors";
    {
        std::ofstream f(path);
        f << kITerm2Content;
    }
    auto theme = termcore::importFromFile(path);
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->background, 0x282a36u);
}

TEST(ThemeImporter, ImportFromFileAlacritty) {
    TempDir dir;
    std::string path = dir.path() + "/Dracula.toml";
    {
        std::ofstream f(path);
        f << kAlacrittyContent;
    }
    auto theme = termcore::importFromFile(path);
    ASSERT_TRUE(theme.has_value());
    verifyDraculaBasicColors(*theme);
}

TEST(ThemeImporter, ImportFromNonExistentFileReturnsNullopt) {
    auto theme = termcore::importFromFile("/nonexistent/file/path.json");
    EXPECT_FALSE(theme.has_value());
}

// --- Kitty import ---

TEST(ThemeImporter, ImportFromKitty) {
    const char* kitty = R"(
background #282a36
foreground #f8f8f2
cursor #f8f8f2
selection_background #44475a
color0 #21222c
color1 #ff5555
color7 #f8f8f2
color15 #ffffff
)";
    auto theme = termcore::importFromKitty(kitty, "KittyTheme");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, "KittyTheme");
    verifyDraculaBasicColors(*theme);
    EXPECT_EQ(theme->palette[0], 0x21222cu);
    EXPECT_EQ(theme->palette[15], 0xffffffu);
}
