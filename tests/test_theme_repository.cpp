#include "termcore/theme_repository.h"

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
                 ("bt_repo_test_" +
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

} // namespace

TEST(ThemeRepository, BuiltinThemesNotEmpty) {
    termcore::ThemeRepository repo;
    auto themes = repo.builtinThemes();
    EXPECT_GE(themes.size(), 6u);

    bool foundDracula = false;
    for (const auto& t : themes) {
        if (t.name == "Dracula") foundDracula = true;
    }
    EXPECT_TRUE(foundDracula);
}

TEST(ThemeRepository, AllThemesIncludesBuiltins) {
    termcore::ThemeRepository repo;
    auto all = repo.allThemes();
    EXPECT_GE(all.size(), 6u);

    // Should be sorted alphabetically
    for (size_t i = 1; i < all.size(); ++i) {
        EXPECT_LE(all[i - 1].name, all[i].name)
            << all[i - 1].name << " should come before " << all[i].name;
    }
}

TEST(ThemeRepository, AllThemeInfosHasMetadata) {
    termcore::ThemeRepository repo;
    auto infos = repo.allThemeInfos();
    EXPECT_GE(infos.size(), 6u);

    bool foundBuiltin = false;
    for (const auto& info : infos) {
        if (info.source == termcore::ThemeSource::Builtin) {
            foundBuiltin = true;
            break;
        }
    }
    EXPECT_TRUE(foundBuiltin);
}

TEST(ThemeRepository, UserThemeDirectoryNotEmpty) {
    termcore::ThemeRepository repo;
    auto dir = repo.userThemeDirectory();
    EXPECT_FALSE(dir.empty());
}

TEST(ThemeRepository, ExportAndImportTheme) {
    TempDir dir;
    termcore::ThemeRepository repo;

    // Export a built-in theme
    std::string exportPath = dir.path() + "/Dracula_export.json";
    ASSERT_TRUE(repo.exportTheme("Dracula", exportPath));

    // Verify the exported file exists and contains valid JSON
    std::ifstream f(exportPath);
    ASSERT_TRUE(f.is_open());
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("Dracula"), std::string::npos);
    EXPECT_NE(content.find("282a36"), std::string::npos);
}

TEST(ThemeRepository, ExportNonExistentThemeFails) {
    TempDir dir;
    termcore::ThemeRepository repo;
    std::string path = dir.path() + "/nothing.json";
    EXPECT_FALSE(repo.exportTheme("NonExistentTheme12345", path));
}

TEST(ThemeRepository, SaveAndLoadUserTheme) {
    TempDir dir;
    termcore::ThemeRepository repo;

    // Create a test theme
    termcore::Theme theme{};
    theme.name = "TestCustomTheme";
    theme.background = 0x112233;
    theme.foreground = 0xaabbcc;
    theme.cursor_color = 0xdddddd;
    theme.selection_background = 0x445566;
    theme.selection_foreground = 0xeeeeff;
    for (int i = 0; i < 16; ++i) {
        theme.palette[i] = 0x100000 * i;
    }

    ASSERT_TRUE(repo.saveUserTheme(theme));

    // Verify the file exists in the user theme directory
    std::string expectedFile = repo.userThemeDirectory() + "/TestCustomTheme.json";
#ifdef _WIN32
    std::replace(expectedFile.begin(), expectedFile.end(), '/', '\\');
#endif
    EXPECT_TRUE(fs::exists(expectedFile));

    // Clean up
    std::error_code ec;
    fs::remove(expectedFile, ec);
}

TEST(ThemeRepository, ImportThemeFromFile) {
    TempDir dir;
    termcore::ThemeRepository repo;

    // Write a Ghostty theme file
    std::string themePath = dir.path() + "/ImportTest";
    {
        std::ofstream f(themePath);
        f << "background = #112233\n";
        f << "foreground = #aabbcc\n";
        f << "cursor-color = #dddddd\n";
        f << "palette = 0=#000000\n";
        f << "palette = 1=#ff0000\n";
    }

    ASSERT_TRUE(repo.importTheme(themePath));

    // The theme should now exist in the user theme directory
    std::string expectedFile =
        repo.userThemeDirectory() + "/ImportTest.json";
#ifdef _WIN32
    std::replace(expectedFile.begin(), expectedFile.end(), '/', '\\');
#endif
    EXPECT_TRUE(fs::exists(expectedFile));

    // Clean up
    repo.deleteUserTheme("ImportTest");
}
