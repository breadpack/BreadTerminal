#include <gtest/gtest.h>
#include "termcore/url_detector.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"

using namespace termcore;

class UrlDetectorTest : public ::testing::Test {
protected:
    UrlDetector detector;
};

// 1. Simple HTTPS URL detected
TEST_F(UrlDetectorTest, SimpleHttpsUrl) {
    auto urls = detector.detectInLine("Visit https://example.com today", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com");
    EXPECT_EQ(urls[0].start_col, 6);
    EXPECT_EQ(urls[0].end_col, 25);
    EXPECT_EQ(urls[0].row, 0);
}

// 2. HTTP URL detected
TEST_F(UrlDetectorTest, HttpUrl) {
    auto urls = detector.detectInLine("http://example.com", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "http://example.com");
}

// 3. URL with path
TEST_F(UrlDetectorTest, UrlWithPath) {
    auto urls = detector.detectInLine("https://example.com/path/to/page", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com/path/to/page");
}

// 4. URL with query
TEST_F(UrlDetectorTest, UrlWithQuery) {
    auto urls = detector.detectInLine("https://example.com?q=test&page=1", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com?q=test&page=1");
}

// 5. URL with fragment
TEST_F(UrlDetectorTest, UrlWithFragment) {
    auto urls = detector.detectInLine("https://example.com#section", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com#section");
}

// 6. URL with port
TEST_F(UrlDetectorTest, UrlWithPort) {
    auto urls = detector.detectInLine("https://localhost:8080/api", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://localhost:8080/api");
}

// 7. www URL detected (prepends http://)
TEST_F(UrlDetectorTest, WwwUrl) {
    auto urls = detector.detectInLine("Visit www.example.com today", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "http://www.example.com");
    EXPECT_EQ(urls[0].start_col, 6);
    EXPECT_EQ(urls[0].end_col, 21);
}

// 8. No URL in plain text
TEST_F(UrlDetectorTest, NoUrlInPlainText) {
    auto urls = detector.detectInLine("This is just plain text", 0);
    EXPECT_TRUE(urls.empty());
}

// 9. Multiple URLs in one line
TEST_F(UrlDetectorTest, MultipleUrls) {
    auto urls = detector.detectInLine(
        "See https://a.com and https://b.com for details", 0);
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0].url, "https://a.com");
    EXPECT_EQ(urls[1].url, "https://b.com");
}

// 10. URL surrounded by angle brackets
TEST_F(UrlDetectorTest, UrlInAngleBrackets) {
    auto urls = detector.detectInLine("<https://example.com>", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com");
    EXPECT_EQ(urls[0].start_col, 1);
    EXPECT_EQ(urls[0].end_col, 20);
}

// 11. URL in parentheses
TEST_F(UrlDetectorTest, UrlInParentheses) {
    auto urls = detector.detectInLine("(https://example.com)", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com");
    EXPECT_EQ(urls[0].start_col, 1);
    EXPECT_EQ(urls[0].end_col, 20);
}

// 12. URL at end of sentence — strips trailing dot
TEST_F(UrlDetectorTest, UrlTrailingDot) {
    auto urls = detector.detectInLine("Visit https://example.com.", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com");
}

// 13. isUrl returns true for valid URLs
TEST_F(UrlDetectorTest, IsUrlTrue) {
    EXPECT_TRUE(UrlDetector::isUrl("https://example.com"));
    EXPECT_TRUE(UrlDetector::isUrl("http://example.com"));
    EXPECT_TRUE(UrlDetector::isUrl("ftp://files.example.com"));
    EXPECT_TRUE(UrlDetector::isUrl("ssh://server.example.com"));
    EXPECT_TRUE(UrlDetector::isUrl("www.example.com"));
}

// 14. isUrl returns false for non-URLs
TEST_F(UrlDetectorTest, IsUrlFalse) {
    EXPECT_FALSE(UrlDetector::isUrl("not a url"));
    EXPECT_FALSE(UrlDetector::isUrl("example.com"));
    EXPECT_FALSE(UrlDetector::isUrl("just text"));
    EXPECT_FALSE(UrlDetector::isUrl(""));
}

// 15. urlAt finds URL at position
TEST_F(UrlDetectorTest, UrlAtFindsUrl) {
    auto urls = detector.detectInLine("Visit https://example.com today", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(detector.urlAt(urls, 0, 6), "https://example.com");
    EXPECT_EQ(detector.urlAt(urls, 0, 10), "https://example.com");
    EXPECT_EQ(detector.urlAt(urls, 0, 24), "https://example.com");
}

// 16. urlAt returns empty for non-URL position
TEST_F(UrlDetectorTest, UrlAtEmpty) {
    auto urls = detector.detectInLine("Visit https://example.com today", 0);
    EXPECT_EQ(detector.urlAt(urls, 0, 0), "");
    EXPECT_EQ(detector.urlAt(urls, 0, 5), "");
    EXPECT_EQ(detector.urlAt(urls, 0, 25), "");
    EXPECT_EQ(detector.urlAt(urls, 1, 10), "");
}

// 17. detectInScreen works with Screen object
TEST_F(UrlDetectorTest, DetectInScreen) {
    Screen screen(3, 40);
    VtParser parser(screen);
    // Write text containing a URL to the screen via VT sequences
    std::string data = "hello\r\nhttps://example.com\r\nworld";
    parser.feed(data.data(), data.size());

    auto urls = detector.detectInScreen(screen);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com");
    EXPECT_EQ(urls[0].row, 1);
}

// 18. ftp:// and ssh:// schemes detected
TEST_F(UrlDetectorTest, FtpAndSshSchemes) {
    auto urls = detector.detectInLine(
        "ftp://files.example.com ssh://server.example.com", 0);
    ASSERT_EQ(urls.size(), 2u);
    EXPECT_EQ(urls[0].url, "ftp://files.example.com");
    EXPECT_EQ(urls[1].url, "ssh://server.example.com");
}

// 19. URL with trailing comma stripped
TEST_F(UrlDetectorTest, UrlTrailingComma) {
    auto urls = detector.detectInLine("See https://example.com, for info", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com");
}

// --- Configurable terminator and trailing punctuation tests ---

// 20. Default terminators match original behavior
TEST_F(UrlDetectorTest, DefaultTerminatorsMatchOriginal) {
    EXPECT_EQ(detector.terminators(), " \t\n\r<>\"'`");
    EXPECT_EQ(detector.trailingPunctuation(), ".,:;!?");
}

// 21. Custom terminator stops URL parsing
TEST_F(UrlDetectorTest, CustomTerminatorStopsUrl) {
    // By default, pipe is not a terminator, so URL includes it
    auto urls1 = detector.detectInLine("https://example.com/path|rest", 0);
    ASSERT_EQ(urls1.size(), 1u);
    EXPECT_EQ(urls1[0].url, "https://example.com/path|rest");

    // Add pipe as terminator
    detector.setTerminators(" \t\n\r<>\"'`|");
    auto urls2 = detector.detectInLine("https://example.com/path|rest", 0);
    ASSERT_EQ(urls2.size(), 1u);
    EXPECT_EQ(urls2[0].url, "https://example.com/path");
}

// 22. Custom trailing punctuation strips different chars
TEST_F(UrlDetectorTest, CustomTrailingPunctuation) {
    // Default: comma is trailing punctuation
    auto urls1 = detector.detectInLine("https://example.com,", 0);
    ASSERT_EQ(urls1.size(), 1u);
    EXPECT_EQ(urls1[0].url, "https://example.com");

    // Remove comma from trailing punctuation — should keep it
    detector.setTrailingPunctuation(".;:!?");
    auto urls2 = detector.detectInLine("https://example.com,", 0);
    ASSERT_EQ(urls2.size(), 1u);
    EXPECT_EQ(urls2[0].url, "https://example.com,");
}

// 23. Empty trailing punctuation means nothing is stripped
TEST_F(UrlDetectorTest, EmptyTrailingPunctuation) {
    detector.setTrailingPunctuation("");
    auto urls = detector.detectInLine("https://example.com.", 0);
    ASSERT_EQ(urls.size(), 1u);
    EXPECT_EQ(urls[0].url, "https://example.com.");
}

// 24. Terminators and trailing punctuation can be set independently
TEST_F(UrlDetectorTest, IndependentTerminatorAndPunctuationConfig) {
    detector.setTerminators(" \t\n");  // minimal terminators
    detector.setTrailingPunctuation(".");  // only dot

    // Angle brackets no longer terminate
    auto urls = detector.detectInLine("<https://example.com/path>.", 0);
    ASSERT_EQ(urls.size(), 1u);
    // < is part of no-scheme start, URL starts at position of https
    // The < is not a terminator anymore, but https:// starts after it
    EXPECT_EQ(urls[0].url, "https://example.com/path>");
}
