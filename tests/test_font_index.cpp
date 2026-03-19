#include <gtest/gtest.h>
#include "termcore/font_index.h"

using namespace termcore;

namespace {

const char* kSampleJSON = R"([
  {"name":"JetBrains Mono","postscript_name":"JetBrainsMono-Regular","category":"monospace","has_ligatures":true,"has_nerd_font_variant":true,"download_url":"https://example.com/jbmono.zip","nerd_font_download_url":"https://example.com/jbmono-nf.zip","license":"OFL-1.1"},
  {"name":"Fira Code","postscript_name":"FiraCode-Regular","category":"monospace","has_ligatures":true,"has_nerd_font_variant":true,"download_url":"https://example.com/firacode.zip","nerd_font_download_url":"https://example.com/firacode-nf.zip","license":"OFL-1.1"},
  {"name":"Hack","postscript_name":"Hack-Regular","category":"monospace","has_ligatures":false,"has_nerd_font_variant":true,"download_url":"https://example.com/hack.zip","nerd_font_download_url":"https://example.com/hack-nf.zip","license":"MIT"},
  {"name":"Monaco","postscript_name":"Monaco","category":"monospace","has_ligatures":false,"has_nerd_font_variant":false,"download_url":"","nerd_font_download_url":"","license":"Apple"},
  {"name":"Menlo","postscript_name":"Menlo-Regular","category":"monospace","has_ligatures":false,"has_nerd_font_variant":false,"download_url":"","nerd_font_download_url":"","license":"Apple"}
])";

} // namespace

TEST(FontIndex, LoadValidJSON) {
    FontIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));
    EXPECT_EQ(index.count(), 5u);

    const auto& fonts = index.all();
    EXPECT_EQ(fonts[0].name, "JetBrains Mono");
    EXPECT_EQ(fonts[0].postscript_name, "JetBrainsMono-Regular");
    EXPECT_EQ(fonts[0].category, "monospace");
    EXPECT_TRUE(fonts[0].has_ligatures);
    EXPECT_TRUE(fonts[0].has_nerd_font_variant);
    EXPECT_EQ(fonts[0].download_url, "https://example.com/jbmono.zip");
    EXPECT_EQ(fonts[0].license, "OFL-1.1");
}

TEST(FontIndex, SearchByName) {
    FontIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    auto results = index.search("jet");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->name, "JetBrains Mono");

    // Case insensitive
    results = index.search("FIRA");
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]->name, "Fira Code");

    // Partial match
    results = index.search("mo");
    ASSERT_EQ(results.size(), 2u); // Monaco, Menlo (contains "mo")
}

TEST(FontIndex, FilterInstalled) {
    FontIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    // Nothing installed initially
    auto installed = index.filter(true, false, false);
    EXPECT_TRUE(installed.empty());

    // Mark one installed
    index.markInstalled("Hack");
    installed = index.filter(true, false, false);
    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0]->name, "Hack");
}

TEST(FontIndex, FilterLigatures) {
    FontIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    auto results = index.filter(false, false, true);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0]->name, "JetBrains Mono");
    EXPECT_EQ(results[1]->name, "Fira Code");
}

TEST(FontIndex, FilterNerdFonts) {
    FontIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    auto results = index.filter(false, true, false);
    ASSERT_EQ(results.size(), 3u);
    // JetBrains Mono, Fira Code, Hack have nerd font variants
    for (const auto* f : results) {
        EXPECT_TRUE(f->has_nerd_font_variant) << f->name;
    }
}

TEST(FontIndex, MarkInstalled) {
    FontIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    // Initially not installed
    const auto& fonts = index.all();
    for (const auto& f : fonts) {
        EXPECT_FALSE(f.installed);
    }

    index.markInstalled("Fira Code");

    auto installed = index.filter(true, false, false);
    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0]->name, "Fira Code");
    EXPECT_TRUE(installed[0]->installed);
}

TEST(FontIndex, RefreshInstallStatusWithPredicate) {
    FontIndex index;
    ASSERT_TRUE(index.loadFromJSON(kSampleJSON));

    // Inject a predicate that says Monaco and Menlo are installed
    index.setInstalledPredicate([](const std::string& psName) -> bool {
        return psName == "Monaco" || psName == "Menlo-Regular";
    });
    index.refreshInstallStatus();

    auto installed = index.filter(true, false, false);
    ASSERT_EQ(installed.size(), 2u);
    EXPECT_EQ(installed[0]->name, "Monaco");
    EXPECT_EQ(installed[1]->name, "Menlo");
}

TEST(FontIndex, LoadInvalidJSON) {
    FontIndex index;
    EXPECT_FALSE(index.loadFromJSON("{not valid json array"));
    EXPECT_FALSE(index.loadFromJSON("just text"));
    EXPECT_EQ(index.count(), 0u);
}

TEST(FontIndex, EmptyJSONArray) {
    FontIndex index;
    ASSERT_TRUE(index.loadFromJSON("[]"));
    EXPECT_EQ(index.count(), 0u);
}
