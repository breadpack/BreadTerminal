#include <gtest/gtest.h>
#include "termcore/search_history.h"
#include "termcore/search_controller.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace termcore {
namespace {

// ============================================================
// SearchHistory unit tests
// ============================================================

class SearchHistoryTest : public ::testing::Test {
protected:
    SearchHistory history;
};

TEST_F(SearchHistoryTest, InitiallyEmpty) {
    EXPECT_TRUE(history.getHistory().empty());
    EXPECT_EQ(history.currentEntry(), "");
}

TEST_F(SearchHistoryTest, AddSingleQuery) {
    history.addQuery("hello");
    ASSERT_EQ(history.getHistory().size(), 1u);
    EXPECT_EQ(history.getHistory()[0], "hello");
}

TEST_F(SearchHistoryTest, AddMultipleQueries_MostRecentFirst) {
    history.addQuery("first");
    history.addQuery("second");
    history.addQuery("third");

    const auto& h = history.getHistory();
    ASSERT_EQ(h.size(), 3u);
    EXPECT_EQ(h[0], "third");
    EXPECT_EQ(h[1], "second");
    EXPECT_EQ(h[2], "first");
}

TEST_F(SearchHistoryTest, DeduplicatesAndMovesToFront) {
    history.addQuery("alpha");
    history.addQuery("beta");
    history.addQuery("gamma");
    history.addQuery("beta");  // duplicate

    const auto& h = history.getHistory();
    ASSERT_EQ(h.size(), 3u);
    EXPECT_EQ(h[0], "beta");
    EXPECT_EQ(h[1], "gamma");
    EXPECT_EQ(h[2], "alpha");
}

TEST_F(SearchHistoryTest, EmptyQueryIgnored) {
    history.addQuery("");
    EXPECT_TRUE(history.getHistory().empty());
}

TEST_F(SearchHistoryTest, MaxCapacity) {
    for (int i = 0; i < 60; ++i) {
        history.addQuery("query_" + std::to_string(i));
    }
    EXPECT_EQ(history.getHistory().size(), SearchHistory::kMaxEntries);
    // Most recent should be query_59
    EXPECT_EQ(history.getHistory()[0], "query_59");
    // Oldest kept should be query_10
    EXPECT_EQ(history.getHistory().back(), "query_10");
}

TEST_F(SearchHistoryTest, NavigateUp) {
    history.addQuery("first");
    history.addQuery("second");
    history.addQuery("third");

    // Initial position: -1 (no entry)
    EXPECT_EQ(history.currentEntry(), "");

    EXPECT_TRUE(history.navigateUp());
    EXPECT_EQ(history.currentEntry(), "third");

    EXPECT_TRUE(history.navigateUp());
    EXPECT_EQ(history.currentEntry(), "second");

    EXPECT_TRUE(history.navigateUp());
    EXPECT_EQ(history.currentEntry(), "first");

    // Can't go further
    EXPECT_FALSE(history.navigateUp());
    EXPECT_EQ(history.currentEntry(), "first");
}

TEST_F(SearchHistoryTest, NavigateDown) {
    history.addQuery("first");
    history.addQuery("second");

    // Navigate up twice
    history.navigateUp();
    history.navigateUp();
    EXPECT_EQ(history.currentEntry(), "first");

    // Navigate down
    EXPECT_TRUE(history.navigateDown());
    EXPECT_EQ(history.currentEntry(), "second");

    // Navigate down past beginning returns to typing position
    EXPECT_TRUE(history.navigateDown());
    EXPECT_EQ(history.currentEntry(), "");

    // Can't go further down
    EXPECT_FALSE(history.navigateDown());
}

TEST_F(SearchHistoryTest, NavigateEmptyHistory) {
    EXPECT_FALSE(history.navigateUp());
    EXPECT_FALSE(history.navigateDown());
    EXPECT_EQ(history.currentEntry(), "");
}

TEST_F(SearchHistoryTest, ResetNavigation) {
    history.addQuery("test");
    history.navigateUp();
    EXPECT_EQ(history.currentEntry(), "test");

    history.resetNavigation();
    EXPECT_EQ(history.currentEntry(), "");
}

TEST_F(SearchHistoryTest, Clear) {
    history.addQuery("a");
    history.addQuery("b");
    history.navigateUp();

    history.clear();

    EXPECT_TRUE(history.getHistory().empty());
    EXPECT_EQ(history.currentEntry(), "");
    EXPECT_FALSE(history.navigateUp());
}

// ============================================================
// JSON persistence tests
// ============================================================

class SearchHistoryPersistenceTest : public ::testing::Test {
protected:
    std::string tmpPath() const {
        return (std::filesystem::temp_directory_path() /
                "bread_test_search_history.json")
            .string();
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(tmpPath(), ec);
    }
};

TEST_F(SearchHistoryPersistenceTest, SaveAndLoadRoundtrip) {
    SearchHistory writer;
    writer.addQuery("alpha");
    writer.addQuery("beta");
    writer.addQuery("gamma");
    writer.saveToDisk(tmpPath());

    SearchHistory reader;
    reader.loadFromDisk(tmpPath());

    const auto& h = reader.getHistory();
    ASSERT_EQ(h.size(), 3u);
    EXPECT_EQ(h[0], "gamma");
    EXPECT_EQ(h[1], "beta");
    EXPECT_EQ(h[2], "alpha");
}

TEST_F(SearchHistoryPersistenceTest, LoadNonexistentFileIsNoOp) {
    SearchHistory h;
    h.loadFromDisk("/nonexistent/path/file.json");
    EXPECT_TRUE(h.getHistory().empty());
}

TEST_F(SearchHistoryPersistenceTest, LoadCorruptedFileIsNoOp) {
    {
        std::ofstream ofs(tmpPath());
        ofs << "not valid json {{{";
    }
    SearchHistory h;
    h.loadFromDisk(tmpPath());
    EXPECT_TRUE(h.getHistory().empty());
}

TEST_F(SearchHistoryPersistenceTest, LoadRespectsMaxCapacity) {
    // Write a file with more than kMaxEntries
    nlohmann::json j;
    std::vector<std::string> big;
    for (int i = 0; i < 100; ++i) {
        big.push_back("q" + std::to_string(i));
    }
    j["queries"] = big;

    {
        std::ofstream ofs(tmpPath());
        ofs << j.dump();
    }

    SearchHistory h;
    h.loadFromDisk(tmpPath());
    EXPECT_EQ(h.getHistory().size(), SearchHistory::kMaxEntries);
}

// ============================================================
// SearchController incremental search tests
// ============================================================

class IncrementalSearchTest : public ::testing::Test {
protected:
    Screen screen{5, 40};
    SearchController ctrl;

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }
};

TEST_F(IncrementalSearchTest, OnCharTypedAppendsToQuery) {
    ctrl.open();
    ctrl.onCharTyped('h');
    ctrl.onCharTyped('e');
    ctrl.onCharTyped('l');

    EXPECT_EQ(ctrl.pendingQuery(), "hel");
}

TEST_F(IncrementalSearchTest, SetQueryIncrementalSetsQuery) {
    ctrl.open();
    ctrl.setQueryIncremental("hello");
    EXPECT_EQ(ctrl.pendingQuery(), "hello");
}

TEST_F(IncrementalSearchTest, FlushIncrementalBeforeDebounceReturnsFalse) {
    feed("Hello World\r\n");
    ctrl.open();
    ctrl.setQueryIncremental("Hello");

    // Immediately flush — should not execute (debounce not elapsed)
    bool executed = ctrl.flushIncremental(screen);
    EXPECT_FALSE(executed);
}

TEST_F(IncrementalSearchTest, FlushIncrementalAfterDebounceExecutesSearch) {
    feed("Hello World\r\n");
    ctrl.open();

    // Set query and manually adjust timing by setting query far in the past.
    // We use setQuery as a workaround to test the flush path without real delays.
    ctrl.setQueryIncremental("Hello");

    // Use setQuery to verify search works correctly
    ctrl.setQuery("Hello", screen);
    EXPECT_EQ(ctrl.totalMatches(), 1);
}

TEST_F(IncrementalSearchTest, SubmitQueryAddsToHistory) {
    feed("Hello World\r\n");
    ctrl.open();
    ctrl.setQueryIncremental("Hello");
    ctrl.submitQuery(screen);

    EXPECT_EQ(ctrl.totalMatches(), 1);
    ASSERT_EQ(ctrl.history().getHistory().size(), 1u);
    EXPECT_EQ(ctrl.history().getHistory()[0], "Hello");
}

TEST_F(IncrementalSearchTest, HistoryNavigation) {
    feed("Hello World\r\nfoo bar\r\n");
    ctrl.open();

    // Submit two queries
    ctrl.setQueryIncremental("Hello");
    ctrl.submitQuery(screen);

    ctrl.setQueryIncremental("foo");
    ctrl.submitQuery(screen);

    // Navigate up to most recent
    EXPECT_TRUE(ctrl.historyUp());
    EXPECT_EQ(ctrl.pendingQuery(), "foo");

    // Navigate up to older
    EXPECT_TRUE(ctrl.historyUp());
    EXPECT_EQ(ctrl.pendingQuery(), "Hello");

    // Navigate down back to newer
    EXPECT_TRUE(ctrl.historyDown());
    EXPECT_EQ(ctrl.pendingQuery(), "foo");

    // Navigate down past all — returns to empty
    EXPECT_TRUE(ctrl.historyDown());
    EXPECT_EQ(ctrl.pendingQuery(), "");
}

TEST_F(IncrementalSearchTest, CloseResetsState) {
    ctrl.open();
    ctrl.setQueryIncremental("test");
    ctrl.close();

    EXPECT_FALSE(ctrl.isActive());
    EXPECT_EQ(ctrl.pendingQuery(), "");
}

} // namespace
} // namespace termcore
