#include <gtest/gtest.h>
#include "termcore/url_highlight.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"

using namespace termcore;

class UrlHighlightTest : public ::testing::Test {
protected:
    UrlHighlightManager mgr;

    // Helper: create a Screen with text fed via VtParser
    Screen makeScreen(int rows, int cols, const std::string& text) {
        Screen screen(rows, cols);
        VtParser parser(screen);
        parser.feed(text.data(), text.size());
        return screen;
    }
};

// --- URL detection in screen content ---

TEST_F(UrlHighlightTest, DetectsUrlInScreenContent) {
    auto screen = makeScreen(3, 60, "hello\r\nhttps://example.com\r\nworld");
    mgr.scanScreen(screen, 3);

    auto urls = mgr.getVisibleUrls();
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com");
    EXPECT_EQ(urls[0].row, 1);
    EXPECT_EQ(urls[0].start_col, 0);
    EXPECT_EQ(urls[0].end_col, 19);
    EXPECT_FALSE(urls[0].hovered);
}

TEST_F(UrlHighlightTest, DetectsMultipleUrlsOnSameLine) {
    auto screen = makeScreen(1, 80, "See https://a.com and https://b.com here");
    mgr.scanScreen(screen, 1);

    auto urls = mgr.getVisibleUrls();
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0].url, "https://a.com");
    EXPECT_EQ(urls[1].url, "https://b.com");
    EXPECT_EQ(urls[0].row, 0);
    EXPECT_EQ(urls[1].row, 0);
}

TEST_F(UrlHighlightTest, DetectsUrlsAcrossMultipleRows) {
    auto screen = makeScreen(3, 60,
        "https://first.com\r\nplain text\r\nhttps://second.com");
    mgr.scanScreen(screen, 3);

    auto urls = mgr.getVisibleUrls();
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0].row, 0);
    EXPECT_EQ(urls[0].url, "https://first.com");
    EXPECT_EQ(urls[1].row, 2);
    EXPECT_EQ(urls[1].url, "https://second.com");
}

// --- Hover detection ---

TEST_F(UrlHighlightTest, HoverInsideUrl) {
    auto screen = makeScreen(1, 60, "Visit https://example.com today");
    mgr.scanScreen(screen, 1);

    // Hover over start of URL
    bool changed = mgr.updateHover(0, 6);
    EXPECT_TRUE(changed);

    auto hovered = mgr.getHoveredUrl();
    ASSERT_TRUE(hovered.has_value());
    EXPECT_EQ(hovered->url, "https://example.com");
    EXPECT_TRUE(hovered->hovered);
}

TEST_F(UrlHighlightTest, HoverOutsideUrl) {
    auto screen = makeScreen(1, 60, "Visit https://example.com today");
    mgr.scanScreen(screen, 1);

    // Hover outside URL region (before)
    mgr.updateHover(0, 0);
    EXPECT_FALSE(mgr.getHoveredUrl().has_value());

    // Hover outside URL region (after)
    mgr.updateHover(0, 30);
    EXPECT_FALSE(mgr.getHoveredUrl().has_value());
}

TEST_F(UrlHighlightTest, HoverTransitionBetweenUrls) {
    auto screen = makeScreen(1, 80, "https://a.com https://b.com");
    mgr.scanScreen(screen, 1);

    // Hover over first URL
    mgr.updateHover(0, 0);
    auto h1 = mgr.getHoveredUrl();
    ASSERT_TRUE(h1.has_value());
    EXPECT_EQ(h1->url, "https://a.com");

    // Move to second URL
    bool changed = mgr.updateHover(0, 14);
    EXPECT_TRUE(changed);
    auto h2 = mgr.getHoveredUrl();
    ASSERT_TRUE(h2.has_value());
    EXPECT_EQ(h2->url, "https://b.com");

    // First URL should no longer be hovered
    auto urls = mgr.getVisibleUrls();
    EXPECT_FALSE(urls[0].hovered);
    EXPECT_TRUE(urls[1].hovered);
}

TEST_F(UrlHighlightTest, ClearHover) {
    auto screen = makeScreen(1, 60, "https://example.com");
    mgr.scanScreen(screen, 1);

    mgr.updateHover(0, 5);
    ASSERT_TRUE(mgr.getHoveredUrl().has_value());

    bool changed = mgr.clearHover();
    EXPECT_TRUE(changed);
    EXPECT_FALSE(mgr.getHoveredUrl().has_value());

    // Clearing again returns false (no change)
    EXPECT_FALSE(mgr.clearHover());
}

TEST_F(UrlHighlightTest, HoverSamePositionNoop) {
    auto screen = makeScreen(1, 60, "https://example.com");
    mgr.scanScreen(screen, 1);

    mgr.updateHover(0, 5);
    // Same hover position should return false (no change)
    bool changed = mgr.updateHover(0, 8);
    EXPECT_FALSE(changed);  // still within same URL
}

// --- Render hints ---

TEST_F(UrlHighlightTest, RenderHintsMatchUrls) {
    auto screen = makeScreen(2, 60,
        "https://a.com\r\nhttps://b.com");
    mgr.scanScreen(screen, 2);
    mgr.updateHover(0, 5);  // hover first URL

    auto hints = mgr.getRenderHints();
    ASSERT_EQ(hints.size(), 2u);
    EXPECT_EQ(hints[0].row, 0);
    EXPECT_TRUE(hints[0].hovered);
    EXPECT_EQ(hints[1].row, 1);
    EXPECT_FALSE(hints[1].hovered);
}

// --- Config enable/disable ---

TEST_F(UrlHighlightTest, DisabledReturnsNoUrls) {
    mgr.setEnabled(false);

    auto screen = makeScreen(1, 60, "https://example.com");
    mgr.scanScreen(screen, 1);

    EXPECT_TRUE(mgr.getVisibleUrls().empty());
    EXPECT_TRUE(mgr.getRenderHints().empty());
    EXPECT_FALSE(mgr.getHoveredUrl().has_value());
}

TEST_F(UrlHighlightTest, DisabledHoverIsNoop) {
    mgr.setEnabled(false);

    auto screen = makeScreen(1, 60, "https://example.com");
    mgr.scanScreen(screen, 1);

    bool changed = mgr.updateHover(0, 5);
    EXPECT_FALSE(changed);
}

TEST_F(UrlHighlightTest, ReenableAfterDisable) {
    auto screen = makeScreen(1, 60, "https://example.com");

    mgr.setEnabled(false);
    mgr.scanScreen(screen, 1);
    EXPECT_TRUE(mgr.getVisibleUrls().empty());

    mgr.setEnabled(true);
    mgr.markDirty();
    mgr.scanScreen(screen, 1);
    EXPECT_EQ(mgr.getVisibleUrls().size(), 1u);
}

TEST_F(UrlHighlightTest, ApplyConfigSetsFields) {
    Config cfg;
    cfg.clickable_urls = false;
    cfg.url_color = 0xff0000;

    mgr.applyConfig(cfg);

    EXPECT_FALSE(mgr.isEnabled());
    EXPECT_EQ(mgr.urlColor(), 0xff0000u);
}

// --- Caching (dirty flag) ---

TEST_F(UrlHighlightTest, CachedResultsWithoutDirty) {
    auto screen = makeScreen(1, 60, "https://example.com");
    mgr.scanScreen(screen, 1);
    ASSERT_EQ(mgr.getVisibleUrls().size(), 1u);

    // Create a different screen, but don't mark dirty
    auto screen2 = makeScreen(1, 60, "no urls here");
    mgr.scanScreen(screen2, 1);

    // Should still have cached result
    EXPECT_EQ(mgr.getVisibleUrls().size(), 1u);
}

TEST_F(UrlHighlightTest, RescanAfterMarkDirty) {
    auto screen = makeScreen(1, 60, "https://example.com");
    mgr.scanScreen(screen, 1);
    ASSERT_EQ(mgr.getVisibleUrls().size(), 1u);

    // Mark dirty and scan with different content
    mgr.markDirty();
    auto screen2 = makeScreen(1, 60, "no urls here");
    mgr.scanScreen(screen2, 1);

    EXPECT_TRUE(mgr.getVisibleUrls().empty());
}

// --- URL spanning visual elements ---

TEST_F(UrlHighlightTest, LongUrlWithPathAndQuery) {
    auto screen = makeScreen(1, 120,
        "Click https://example.com/path/to/page?q=search&lang=en#top end");
    mgr.scanScreen(screen, 1);

    auto urls = mgr.getVisibleUrls();
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com/path/to/page?q=search&lang=en#top");
    EXPECT_EQ(urls[0].start_col, 6);
}

TEST_F(UrlHighlightTest, WwwUrlDetected) {
    auto screen = makeScreen(1, 60, "See www.example.com for info");
    mgr.scanScreen(screen, 1);

    auto urls = mgr.getVisibleUrls();
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "http://www.example.com");
}
