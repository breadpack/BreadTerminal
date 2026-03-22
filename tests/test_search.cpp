#include <gtest/gtest.h>
#include "termcore/search.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include <string>

namespace termcore {
namespace {

class SearchTest : public ::testing::Test {
protected:
    Screen screen{5, 20};
    TerminalSearch search;

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }
};

// 1. Search empty screen → 0 matches
TEST_F(SearchTest, SearchEmptyScreen) {
    int count = search.search(screen, "Hello");
    EXPECT_EQ(count, 0);
    EXPECT_EQ(search.matchCount(), 0u);
    EXPECT_EQ(search.currentIndex(), -1);
    EXPECT_EQ(search.currentMatch(), nullptr);
}

// 2. Search "Hello" → 2 matches
TEST_F(SearchTest, SearchFindsMultipleMatches) {
    feed("Hello World\r\nfoo bar baz\r\nHello Again\r\n");
    int count = search.search(screen, "Hello");
    EXPECT_EQ(count, 2);
    EXPECT_EQ(search.matchCount(), 2u);
}

// 3. Match positions are correct (row, col)
TEST_F(SearchTest, MatchPositionsCorrect) {
    feed("Hello World\r\nfoo bar baz\r\nHello Again\r\n");
    search.search(screen, "Hello");

    ASSERT_GE(search.matchCount(), 2u);
    const auto& matches = search.matches();

    EXPECT_EQ(matches[0].row, 0);
    EXPECT_EQ(matches[0].start_col, 0);
    EXPECT_EQ(matches[0].end_col, 5);

    EXPECT_EQ(matches[1].row, 2);
    EXPECT_EQ(matches[1].start_col, 0);
    EXPECT_EQ(matches[1].end_col, 5);
}

// 4. Case insensitive: "hello" finds "Hello"
TEST_F(SearchTest, CaseInsensitiveSearch) {
    feed("Hello World\r\n");
    SearchOptions opts;
    opts.case_sensitive = false;
    int count = search.search(screen, "hello", opts);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(search.matches()[0].row, 0);
    EXPECT_EQ(search.matches()[0].start_col, 0);
}

// 5. Case sensitive: "hello" doesn't find "Hello"
TEST_F(SearchTest, CaseSensitiveSearch) {
    feed("Hello World\r\n");
    SearchOptions opts;
    opts.case_sensitive = true;
    int count = search.search(screen, "hello", opts);
    EXPECT_EQ(count, 0);
}

// 6. next() cycles through matches
TEST_F(SearchTest, NextCyclesThroughMatches) {
    feed("Hello World\r\nfoo bar baz\r\nHello Again\r\n");
    search.search(screen, "Hello");

    EXPECT_EQ(search.currentIndex(), 0);

    const SearchMatch* m = search.next();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(search.currentIndex(), 1);

    // Wrap around back to 0
    m = search.next();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(search.currentIndex(), 0);
}

// 7. prev() goes backward
TEST_F(SearchTest, PrevGoesBackward) {
    feed("Hello World\r\nfoo bar baz\r\nHello Again\r\n");
    search.search(screen, "Hello");

    EXPECT_EQ(search.currentIndex(), 0);

    // prev from 0 wraps to last
    const SearchMatch* m = search.prev();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(search.currentIndex(), 1);

    m = search.prev();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(search.currentIndex(), 0);
}

// 8. Wrap around works
TEST_F(SearchTest, WrapAroundWorks) {
    feed("Hello World\r\n");
    SearchOptions opts;
    opts.wrap_around = true;
    search.search(screen, "Hello", opts);

    EXPECT_EQ(search.matchCount(), 1u);
    EXPECT_EQ(search.currentIndex(), 0);

    // next wraps back to 0
    const SearchMatch* m = search.next();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(search.currentIndex(), 0);

    // prev wraps back to 0
    m = search.prev();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(search.currentIndex(), 0);
}

// 9. No wrap around: next past end returns nullptr
TEST_F(SearchTest, NoWrapAroundReturnsNullptr) {
    feed("Hello World\r\n");
    SearchOptions opts;
    opts.wrap_around = false;
    search.search(screen, "Hello", opts);

    EXPECT_EQ(search.currentIndex(), 0);

    // next past end
    const SearchMatch* m = search.next();
    EXPECT_EQ(m, nullptr);

    // prev past beginning
    search.search(screen, "Hello", opts);  // reset
    m = search.prev();
    EXPECT_EQ(m, nullptr);
}

// 10. clear() resets state
TEST_F(SearchTest, ClearResetsState) {
    feed("Hello World\r\n");
    search.search(screen, "Hello");

    EXPECT_TRUE(search.isActive());
    EXPECT_GT(search.matchCount(), 0u);

    search.clear();

    EXPECT_FALSE(search.isActive());
    EXPECT_EQ(search.matchCount(), 0u);
    EXPECT_EQ(search.currentIndex(), -1);
    EXPECT_EQ(search.currentMatch(), nullptr);
    EXPECT_TRUE(search.query().empty());
}

// 11. Search with no matches → 0
TEST_F(SearchTest, SearchNoMatches) {
    feed("Hello World\r\n");
    int count = search.search(screen, "xyz");
    EXPECT_EQ(count, 0);
    EXPECT_EQ(search.matchCount(), 0u);
    EXPECT_EQ(search.currentIndex(), -1);
}

// 12. isActive after search → true
TEST_F(SearchTest, IsActiveAfterSearch) {
    feed("Hello World\r\n");
    search.search(screen, "Hello");
    EXPECT_TRUE(search.isActive());
}

// 13. Multiple matches in same line
TEST_F(SearchTest, MultipleMatchesSameLine) {
    feed("abcabcabc\r\n");
    int count = search.search(screen, "abc");
    EXPECT_EQ(count, 3);

    const auto& matches = search.matches();
    EXPECT_EQ(matches[0].start_col, 0);
    EXPECT_EQ(matches[1].start_col, 3);
    EXPECT_EQ(matches[2].start_col, 6);
}

// 14. nearestTo finds closest match
TEST_F(SearchTest, NearestToFindsClosest) {
    // Use a larger screen so no scrollback is involved
    Screen big(10, 20);
    VtParser parser(big);
    std::string data = "Hello\r\nfoo\r\nbar\r\nbaz\r\nHello\r\n";
    parser.feed(data.data(), data.size());

    TerminalSearch ts;
    ts.search(big, "Hello");

    ASSERT_EQ(ts.matchCount(), 2u);

    // Matches at row 0 and row 4 — nearest to row 3 should be row 4
    const SearchMatch* m = ts.nearestTo(3);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->row, 4);

    // Nearest to row 1 should be row 0
    m = ts.nearestTo(1);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->row, 0);
}

// 15. matchCount returns correct count
TEST_F(SearchTest, MatchCountCorrect) {
    feed("aaa\r\nbbb\r\naaa\r\n");
    search.search(screen, "aaa");
    EXPECT_EQ(search.matchCount(), 2u);

    search.search(screen, "bbb");
    EXPECT_EQ(search.matchCount(), 1u);

    search.search(screen, "zzz");
    EXPECT_EQ(search.matchCount(), 0u);
}

// Empty query returns 0 matches
TEST_F(SearchTest, EmptyQueryReturnsZero) {
    feed("Hello World\r\n");
    int count = search.search(screen, "");
    EXPECT_EQ(count, 0);
    EXPECT_FALSE(search.isActive());
}

// --- Regex search tests ---

// Basic regex match
TEST_F(SearchTest, RegexBasicMatch) {
    feed("Hello World\r\nfoo 123 bar\r\nHello Again\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    int count = search.search(screen, "\\d+", opts);
    EXPECT_EQ(count, 1);
    ASSERT_EQ(search.matchCount(), 1u);
    EXPECT_EQ(search.matches()[0].row, 1);
    EXPECT_EQ(search.matches()[0].start_col, 4);
    EXPECT_EQ(search.matches()[0].end_col, 7);
}

// Regex with alternation
TEST_F(SearchTest, RegexAlternation) {
    feed("cat\r\ndog\r\nbird\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    int count = search.search(screen, "cat|dog", opts);
    EXPECT_EQ(count, 2);
}

// Case-insensitive regex
TEST_F(SearchTest, RegexCaseInsensitive) {
    feed("Hello World\r\nhello world\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    opts.case_sensitive = false;
    int count = search.search(screen, "hello", opts);
    EXPECT_EQ(count, 2);
}

// Case-sensitive regex
TEST_F(SearchTest, RegexCaseSensitive) {
    feed("Hello World\r\nhello world\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    opts.case_sensitive = true;
    int count = search.search(screen, "hello", opts);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(search.matches()[0].row, 1);
}

// Invalid regex pattern returns 0 matches without crashing
TEST_F(SearchTest, RegexInvalidPattern) {
    feed("Hello World\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    int count = search.search(screen, "[invalid(", opts);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(search.matchCount(), 0u);
}

// Regex with special characters (literal dot via escape)
TEST_F(SearchTest, RegexSpecialCharacters) {
    feed("file.txt\r\nfiletxt\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    int count = search.search(screen, "file\\.txt", opts);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(search.matches()[0].row, 0);
}

// Regex with dot matches any character
TEST_F(SearchTest, RegexDotMatchesAny) {
    feed("file.txt\r\nfiletxt\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    int count = search.search(screen, "file.txt", opts);
    EXPECT_EQ(count, 2);
}

// Multiple regex matches on same line
TEST_F(SearchTest, RegexMultipleMatchesSameLine) {
    feed("abc 123 def 456\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    int count = search.search(screen, "\\d+", opts);
    EXPECT_EQ(count, 2);
    EXPECT_EQ(search.matches()[0].start_col, 4);
    EXPECT_EQ(search.matches()[0].end_col, 7);
    EXPECT_EQ(search.matches()[1].start_col, 12);
    EXPECT_EQ(search.matches()[1].end_col, 15);
}

// Regex no matches
TEST_F(SearchTest, RegexNoMatches) {
    feed("Hello World\r\n");
    SearchOptions opts;
    opts.use_regex = true;
    int count = search.search(screen, "\\d+", opts);
    EXPECT_EQ(count, 0);
}

// Literal search unchanged when use_regex is false
TEST_F(SearchTest, LiteralSearchUnchangedWithRegexOff) {
    feed("file.txt\r\nfiletxt\r\n");
    SearchOptions opts;
    opts.use_regex = false;
    // "." is literal, not regex dot
    int count = search.search(screen, "file.txt", opts);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(search.matches()[0].row, 0);
}

} // namespace
} // namespace termcore
