#include <gtest/gtest.h>
#include "termcore/theme_index.h"

using namespace termcore;

namespace {

const char* kSampleJSON = R"([
  {"name":"Dracula","background":"282a36","foreground":"f8f8f2","palette":["21222c","ff5555","50fa7b","f1fa8c","bd93f9","ff79c6","8be9fd","f8f8f2","6272a4","ff6e6e","69ff94","ffffa5","d6acff","ff92df","a4ffff","ffffff"],"source_url":"https://example.com/Dracula"},
  {"name":"Nord","background":"2e3440","foreground":"d8dee9","palette":["3b4252","bf616a","a3be8c","ebcb8b","81a1c1","b48ead","88c0d0","e5e9f0","4c566a","bf616a","a3be8c","ebcb8b","81a1c1","b48ead","8fbcbb","eceff4"],"source_url":"https://example.com/Nord"},
  {"name":"Solarized Dark","background":"002b36","foreground":"839496","palette":["073642","dc322f","859900","b58900","268bd2","d33682","2aa198","eee8d5","002b36","cb4b16","586e75","657b83","839496","6c71c4","93a1a1","fdf6e3"],"source_url":"https://example.com/Solarized+Dark"},
  {"name":"Solarized Light","background":"fdf6e3","foreground":"657b83","palette":["073642","dc322f","859900","b58900","268bd2","d33682","2aa198","eee8d5","002b36","cb4b16","586e75","657b83","839496","6c71c4","93a1a1","fdf6e3"],"source_url":"https://example.com/Solarized+Light"},
  {"name":"Gruvbox Light","background":"fbf1c7","foreground":"3c3836","palette":["fbf1c7","cc241d","98971a","d79921","458588","b16286","689d6a","7c6f64","928374","9d0006","79740e","b57614","076678","8f3f71","427b58","3c3836"],"source_url":"https://example.com/Gruvbox+Light"}
])";

} // namespace

TEST(ThemeIndex, LoadValidJSON) {
    ThemeIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));
    EXPECT_EQ(index.count(), 5u);

    const auto& themes = index.all();
    EXPECT_EQ(themes[0].name, "Dracula");
    EXPECT_EQ(themes[0].background, 0x282a36u);
    EXPECT_EQ(themes[0].foreground, 0xf8f8f2u);
    EXPECT_EQ(themes[0].palette[0], 0x21222cu);
    EXPECT_EQ(themes[0].palette[1], 0xff5555u);
    EXPECT_EQ(themes[0].source_url, "https://example.com/Dracula");
}

TEST(ThemeIndex, SearchByName) {
    ThemeIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    auto results = index.search("drac");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->name, "Dracula");
}

TEST(ThemeIndex, SearchCaseInsensitive) {
    ThemeIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    auto results = index.search("NORD");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->name, "Nord");
}

TEST(ThemeIndex, FilterDark) {
    ThemeIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    auto results = index.filterByCategory(true, false, false);
    // Dracula, Nord, Solarized Dark are dark
    for (const auto* t : results) {
        EXPECT_TRUE(t->is_dark) << t->name << " should be dark";
    }
    // Solarized Light and Gruvbox Light should not appear
    for (const auto* t : results) {
        EXPECT_NE(t->name, "Solarized Light");
        EXPECT_NE(t->name, "Gruvbox Light");
    }
}

TEST(ThemeIndex, FilterLight) {
    ThemeIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    auto results = index.filterByCategory(false, true, false);
    // Should only have Solarized Light and Gruvbox Light
    ASSERT_EQ(results.size(), 2u);
    for (const auto* t : results) {
        EXPECT_FALSE(t->is_dark) << t->name << " should be light";
    }
}

TEST(ThemeIndex, MarkInstalled) {
    ThemeIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    // Initially nothing is installed
    auto installed = index.filterByCategory(false, false, true);
    EXPECT_TRUE(installed.empty());

    // Mark Dracula as installed
    index.markInstalled("Dracula");

    installed = index.filterByCategory(false, false, true);
    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0]->name, "Dracula");
    EXPECT_TRUE(installed[0]->installed);
}

TEST(ThemeIndex, LoadInvalidJSONReturnsFalse) {
    ThemeIndex index;
    EXPECT_FALSE(index.loadFromJSON("{not valid json array"));
    EXPECT_FALSE(index.loadFromJSON("just text"));
}

TEST(ThemeIndex, EmptyJSONReturnsTrue) {
    ThemeIndex index;
    ASSERT_TRUE(index.loadFromJSON("[]"));
    EXPECT_EQ(index.count(), 0u);
}
