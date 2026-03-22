#include <gtest/gtest.h>
#include "termcore/clipboard_history.h"

#include <thread>

using namespace termcore;

class ClipboardHistoryTest : public ::testing::Test {
protected:
    ClipboardHistory history;
};

TEST_F(ClipboardHistoryTest, InitiallyEmpty) {
    EXPECT_EQ(history.size(), 0u);
    EXPECT_TRUE(history.getEntries().empty());
}

TEST_F(ClipboardHistoryTest, AddSingleEntry) {
    history.addEntry("hello");
    EXPECT_EQ(history.size(), 1u);
    EXPECT_EQ(history.getEntry(0), "hello");
}

TEST_F(ClipboardHistoryTest, EmptyStringIgnored) {
    history.addEntry("");
    EXPECT_EQ(history.size(), 0u);
}

TEST_F(ClipboardHistoryTest, MostRecentFirst) {
    history.addEntry("first");
    history.addEntry("second");
    history.addEntry("third");

    EXPECT_EQ(history.size(), 3u);
    EXPECT_EQ(history.getEntry(0), "third");
    EXPECT_EQ(history.getEntry(1), "second");
    EXPECT_EQ(history.getEntry(2), "first");
}

TEST_F(ClipboardHistoryTest, DeduplicatesMovesToFront) {
    history.addEntry("alpha");
    history.addEntry("beta");
    history.addEntry("gamma");

    // Re-add "alpha" — should move to front, not duplicate
    history.addEntry("alpha");

    EXPECT_EQ(history.size(), 3u);
    EXPECT_EQ(history.getEntry(0), "alpha");
    EXPECT_EQ(history.getEntry(1), "gamma");
    EXPECT_EQ(history.getEntry(2), "beta");
}

TEST_F(ClipboardHistoryTest, MaxCapacity) {
    for (int i = 0; i < 25; ++i) {
        history.addEntry("item_" + std::to_string(i));
    }

    EXPECT_EQ(history.size(), ClipboardHistory::kMaxEntries);
    // Most recent should be item_24
    EXPECT_EQ(history.getEntry(0), "item_24");
    // Oldest kept should be item_5 (items 0-4 evicted)
    EXPECT_EQ(history.getEntry(19), "item_5");
}

TEST_F(ClipboardHistoryTest, Clear) {
    history.addEntry("a");
    history.addEntry("b");
    EXPECT_EQ(history.size(), 2u);

    history.clear();
    EXPECT_EQ(history.size(), 0u);
    EXPECT_TRUE(history.getEntries().empty());
}

TEST_F(ClipboardHistoryTest, GetEntryOutOfRange) {
    history.addEntry("only");
    EXPECT_EQ(history.getEntry(-1), "");
    EXPECT_EQ(history.getEntry(1), "");
    EXPECT_EQ(history.getEntry(100), "");
}

TEST_F(ClipboardHistoryTest, PreviewTruncatesLongText) {
    std::string longText(200, 'x');
    history.addEntry(longText);

    const auto& entries = history.getEntries();
    ASSERT_EQ(entries.size(), 1u);
    // Preview should be at most 80 chars + "..."
    EXPECT_LE(entries[0].preview.size(), ClipboardHistory::kPreviewMaxLength + 3);
    EXPECT_TRUE(entries[0].preview.find("...") != std::string::npos);
}

TEST_F(ClipboardHistoryTest, PreviewTruncatesAtNewline) {
    history.addEntry("first line\nsecond line\nthird line");

    const auto& entries = history.getEntries();
    ASSERT_EQ(entries.size(), 1u);
    // Preview should only contain the first line (plus "...")
    EXPECT_EQ(entries[0].preview, "first line...");
}

TEST_F(ClipboardHistoryTest, PreviewShortTextUnchanged) {
    history.addEntry("short");

    const auto& entries = history.getEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].preview, "short");
}

TEST_F(ClipboardHistoryTest, TimestampsAreSet) {
    auto before = std::chrono::system_clock::now();
    history.addEntry("timed");
    auto after = std::chrono::system_clock::now();

    const auto& entries = history.getEntries();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_GE(entries[0].timestamp, before);
    EXPECT_LE(entries[0].timestamp, after);
}

TEST_F(ClipboardHistoryTest, TimestampsOrdering) {
    history.addEntry("first");
    // Small delay to ensure different timestamps
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    history.addEntry("second");

    const auto& entries = history.getEntries();
    ASSERT_EQ(entries.size(), 2u);
    // Most recent (index 0) should have a later or equal timestamp
    EXPECT_GE(entries[0].timestamp, entries[1].timestamp);
}

TEST_F(ClipboardHistoryTest, DeduplicateUpdatesTimestamp) {
    history.addEntry("reused");
    auto firstTime = history.getEntries()[0].timestamp;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    history.addEntry("reused");
    auto secondTime = history.getEntries()[0].timestamp;

    EXPECT_GT(secondTime, firstTime);
}
