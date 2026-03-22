#include <gtest/gtest.h>
#include "termcore/terminal_inspector.h"

using namespace termcore;

class TerminalInspectorTest : public ::testing::Test {
protected:
    TerminalInspector inspector;
};

// --- Enable/disable ---

TEST_F(TerminalInspectorTest, DisabledByDefault) {
    EXPECT_FALSE(inspector.isEnabled());
}

TEST_F(TerminalInspectorTest, SetEnabled) {
    inspector.setEnabled(true);
    EXPECT_TRUE(inspector.isEnabled());
    inspector.setEnabled(false);
    EXPECT_FALSE(inspector.isEnabled());
}

TEST_F(TerminalInspectorTest, Toggle) {
    EXPECT_FALSE(inspector.isEnabled());
    inspector.toggle();
    EXPECT_TRUE(inspector.isEnabled());
    inspector.toggle();
    EXPECT_FALSE(inspector.isEnabled());
}

TEST_F(TerminalInspectorTest, DoesNotLogWhenDisabled) {
    inspector.logText("hello");
    inspector.logCSI("\x1b[1m", "SGR: Bold");
    EXPECT_EQ(inspector.entryCount(), 0u);
}

// --- Logging different types ---

TEST_F(TerminalInspectorTest, LogText) {
    inspector.setEnabled(true);
    inspector.logText("hello");
    ASSERT_EQ(inspector.entryCount(), 1u);
    EXPECT_EQ(inspector.entries()[0].type, InspectorEntry::Type::Text);
    EXPECT_EQ(inspector.entries()[0].raw, "hello");
    EXPECT_EQ(inspector.entries()[0].sequence_number, 0);
}

TEST_F(TerminalInspectorTest, LogCSI) {
    inspector.setEnabled(true);
    inspector.logCSI("\x1b[1m", "SGR: Bold On");
    ASSERT_EQ(inspector.entryCount(), 1u);
    EXPECT_EQ(inspector.entries()[0].type, InspectorEntry::Type::CSI);
    EXPECT_EQ(inspector.entries()[0].description, "SGR: Bold On");
}

TEST_F(TerminalInspectorTest, LogOSC) {
    inspector.setEnabled(true);
    inspector.logOSC(0, "title", "OSC 0: Set title");
    ASSERT_EQ(inspector.entryCount(), 1u);
    EXPECT_EQ(inspector.entries()[0].type, InspectorEntry::Type::OSC);
    EXPECT_EQ(inspector.entries()[0].description, "OSC 0: Set title");
}

TEST_F(TerminalInspectorTest, LogESC) {
    inspector.setEnabled(true);
    inspector.logESC("\x1b" "7", "ESC 7: Save Cursor");
    ASSERT_EQ(inspector.entryCount(), 1u);
    EXPECT_EQ(inspector.entries()[0].type, InspectorEntry::Type::ESC);
}

TEST_F(TerminalInspectorTest, LogDCS) {
    inspector.setEnabled(true);
    inspector.logDCS("\x1bPq", "DCS: Sixel");
    ASSERT_EQ(inspector.entryCount(), 1u);
    EXPECT_EQ(inspector.entries()[0].type, InspectorEntry::Type::DCS);
}

TEST_F(TerminalInspectorTest, LogControl) {
    inspector.setEnabled(true);
    inspector.logControl('\n', "LF");
    ASSERT_EQ(inspector.entryCount(), 1u);
    EXPECT_EQ(inspector.entries()[0].type, InspectorEntry::Type::Control);
    EXPECT_EQ(inspector.entries()[0].description, "LF");
}

TEST_F(TerminalInspectorTest, SequenceNumbersIncrement) {
    inspector.setEnabled(true);
    inspector.logText("a");
    inspector.logText("b");
    inspector.logText("c");
    ASSERT_EQ(inspector.entryCount(), 3u);
    EXPECT_EQ(inspector.entries()[0].sequence_number, 0);
    EXPECT_EQ(inspector.entries()[1].sequence_number, 1);
    EXPECT_EQ(inspector.entries()[2].sequence_number, 2);
}

// --- Max entries (ring buffer) ---

TEST_F(TerminalInspectorTest, MaxEntriesDefault) {
    EXPECT_EQ(inspector.maxEntries(), 10000u);
}

TEST_F(TerminalInspectorTest, RingBufferEvictsOldEntries) {
    inspector.setEnabled(true);
    inspector.setMaxEntries(3);
    inspector.logText("a");
    inspector.logText("b");
    inspector.logText("c");
    EXPECT_EQ(inspector.entryCount(), 3u);

    inspector.logText("d");
    EXPECT_EQ(inspector.entryCount(), 3u);
    // First entry should have been evicted; oldest is now "b"
    EXPECT_EQ(inspector.entries()[0].raw, "b");
    EXPECT_EQ(inspector.entries()[1].raw, "c");
    EXPECT_EQ(inspector.entries()[2].raw, "d");
}

TEST_F(TerminalInspectorTest, SetMaxEntriesTrimsExisting) {
    inspector.setEnabled(true);
    inspector.logText("a");
    inspector.logText("b");
    inspector.logText("c");
    inspector.logText("d");
    inspector.logText("e");
    EXPECT_EQ(inspector.entryCount(), 5u);

    inspector.setMaxEntries(2);
    EXPECT_EQ(inspector.entryCount(), 2u);
    EXPECT_EQ(inspector.entries()[0].raw, "d");
    EXPECT_EQ(inspector.entries()[1].raw, "e");
}

// --- Filtering ---

TEST_F(TerminalInspectorTest, AllTypesVisibleByDefault) {
    EXPECT_TRUE(inspector.isTypeVisible(InspectorEntry::Type::Text));
    EXPECT_TRUE(inspector.isTypeVisible(InspectorEntry::Type::CSI));
    EXPECT_TRUE(inspector.isTypeVisible(InspectorEntry::Type::OSC));
    EXPECT_TRUE(inspector.isTypeVisible(InspectorEntry::Type::ESC));
    EXPECT_TRUE(inspector.isTypeVisible(InspectorEntry::Type::DCS));
    EXPECT_TRUE(inspector.isTypeVisible(InspectorEntry::Type::Control));
}

TEST_F(TerminalInspectorTest, FilterByType) {
    inspector.setEnabled(true);
    inspector.logText("hello");
    inspector.logCSI("\x1b[1m", "SGR");
    inspector.logControl('\n', "LF");

    // Hide Text type
    inspector.setTypeFilter(InspectorEntry::Type::Text, false);
    EXPECT_FALSE(inspector.isTypeVisible(InspectorEntry::Type::Text));

    auto filtered = inspector.filteredEntries();
    EXPECT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0]->type, InspectorEntry::Type::CSI);
    EXPECT_EQ(filtered[1]->type, InspectorEntry::Type::Control);
}

TEST_F(TerminalInspectorTest, FilterShowsOnlyMatchingTypes) {
    inspector.setEnabled(true);
    inspector.logText("a");
    inspector.logCSI("\x1b[m", "SGR Reset");
    inspector.logOSC(0, "title", "OSC 0");
    inspector.logESC("\x1b7", "DECSC");
    inspector.logDCS("\x1bPq", "Sixel");
    inspector.logControl('\r', "CR");

    // Hide everything except CSI
    inspector.setTypeFilter(InspectorEntry::Type::Text, false);
    inspector.setTypeFilter(InspectorEntry::Type::OSC, false);
    inspector.setTypeFilter(InspectorEntry::Type::ESC, false);
    inspector.setTypeFilter(InspectorEntry::Type::DCS, false);
    inspector.setTypeFilter(InspectorEntry::Type::Control, false);

    auto filtered = inspector.filteredEntries();
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0]->type, InspectorEntry::Type::CSI);
}

// --- Pause/resume ---

TEST_F(TerminalInspectorTest, NotPausedByDefault) {
    EXPECT_FALSE(inspector.isPaused());
}

TEST_F(TerminalInspectorTest, PauseStopsCapture) {
    inspector.setEnabled(true);
    inspector.logText("before");
    EXPECT_EQ(inspector.entryCount(), 1u);

    inspector.pause();
    EXPECT_TRUE(inspector.isPaused());
    inspector.logText("during pause");
    EXPECT_EQ(inspector.entryCount(), 1u); // no new entry

    inspector.resume();
    EXPECT_FALSE(inspector.isPaused());
    inspector.logText("after");
    EXPECT_EQ(inspector.entryCount(), 2u);
}

TEST_F(TerminalInspectorTest, PauseKeepsExistingEntries) {
    inspector.setEnabled(true);
    inspector.logText("a");
    inspector.logText("b");
    inspector.pause();
    EXPECT_EQ(inspector.entryCount(), 2u);
    EXPECT_EQ(inspector.entries()[0].raw, "a");
    EXPECT_EQ(inspector.entries()[1].raw, "b");
}

// --- Clear ---

TEST_F(TerminalInspectorTest, Clear) {
    inspector.setEnabled(true);
    inspector.logText("a");
    inspector.logCSI("\x1b[1m", "SGR");
    EXPECT_EQ(inspector.entryCount(), 2u);

    inspector.clear();
    EXPECT_EQ(inspector.entryCount(), 0u);
    EXPECT_TRUE(inspector.entries().empty());
}

TEST_F(TerminalInspectorTest, ClearResetsSequenceNumbers) {
    inspector.setEnabled(true);
    inspector.logText("a");
    inspector.logText("b");
    inspector.clear();

    inspector.logText("c");
    ASSERT_EQ(inspector.entryCount(), 1u);
    EXPECT_EQ(inspector.entries()[0].sequence_number, 0);
}
