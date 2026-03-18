#include <gtest/gtest.h>
#include "CoreTextDiscovery.h"

namespace termcore {
namespace {

class CoreTextDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        discovery_ = createCoreTextDiscovery();
    }

    std::unique_ptr<IFontDiscovery> discovery_;
};

TEST_F(CoreTextDiscoveryTest, CreateDiscoveryNotNull) {
    ASSERT_NE(discovery_, nullptr);
}

TEST_F(CoreTextDiscoveryTest, FindFontsMenloReturnsResults) {
    FontQuery query;
    query.family = "Menlo";
    auto results = discovery_->findFonts(query);
    EXPECT_FALSE(results.empty());
}

TEST_F(CoreTextDiscoveryTest, FindFontsMenloHasFilePath) {
    FontQuery query;
    query.family = "Menlo";
    auto results = discovery_->findFonts(query);
    ASSERT_FALSE(results.empty());
    EXPECT_FALSE(results[0].file_path.empty());
}

TEST_F(CoreTextDiscoveryTest, FindFontsNonExistentReturnsEmpty) {
    FontQuery query;
    query.family = "NonExistentFontXYZ";
    auto results = discovery_->findFonts(query);
    EXPECT_TRUE(results.empty());
}

TEST_F(CoreTextDiscoveryTest, FindFontsBoldStyle) {
    FontQuery query;
    query.family = "Menlo";
    query.style = FontStyle::Bold;
    auto results = discovery_->findFonts(query);
    ASSERT_FALSE(results.empty());

    bool hasBold = false;
    for (const auto& fd : results) {
        if (fd.style == FontStyle::Bold || fd.style == FontStyle::BoldItalic) {
            hasBold = true;
            break;
        }
    }
    EXPECT_TRUE(hasBold) << "Expected at least one bold result for Menlo Bold query";
}

TEST_F(CoreTextDiscoveryTest, DefaultMonospaceValid) {
    auto fd = discovery_->defaultMonospace();
    EXPECT_FALSE(fd.family.empty());
    EXPECT_FALSE(fd.file_path.empty());
}

TEST_F(CoreTextDiscoveryTest, FindFallbackForAsciiA) {
    auto fd = discovery_->findFallback(U'A', FontStyle::Regular);
    EXPECT_FALSE(fd.family.empty());
    EXPECT_FALSE(fd.file_path.empty());
}

TEST_F(CoreTextDiscoveryTest, FindFallbackForCJK) {
    // U+4E2D = '中'
    auto fd = discovery_->findFallback(U'\u4E2D', FontStyle::Regular);
    EXPECT_FALSE(fd.family.empty());
    EXPECT_FALSE(fd.file_path.empty());
}

TEST_F(CoreTextDiscoveryTest, FindFallbackForEmoji) {
    // U+1F600 = 😀
    auto fd = discovery_->findFallback(U'\U0001F600', FontStyle::Regular);
    EXPECT_FALSE(fd.family.empty());
    EXPECT_FALSE(fd.file_path.empty());
}

} // namespace
} // namespace termcore
