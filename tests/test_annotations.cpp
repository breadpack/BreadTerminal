#include "termcore/annotations.h"

#include <gtest/gtest.h>

using namespace termcore;

TEST(AnnotationManager, AddAndRetrieve) {
    AnnotationManager mgr;
    int id = mgr.addAnnotation(10, "Test note");
    EXPECT_EQ(id, 1);
    EXPECT_EQ(mgr.count(), 1u);

    const auto& all = mgr.annotations();
    ASSERT_TRUE(all.count(id));
    EXPECT_EQ(all.at(id).absoluteRow, 10);
    EXPECT_EQ(all.at(id).text, "Test note");
    EXPECT_EQ(all.at(id).startCol, -1);
    EXPECT_EQ(all.at(id).endCol, -1);
    EXPECT_EQ(all.at(id).color, 0xFFFF00u);
}

TEST(AnnotationManager, AddWithColumnRange) {
    AnnotationManager mgr;
    int id = mgr.addAnnotation(5, "Column note", 3, 10, 0xFF0000);
    EXPECT_EQ(mgr.count(), 1u);

    const auto& ann = mgr.annotations().at(id);
    EXPECT_EQ(ann.startCol, 3);
    EXPECT_EQ(ann.endCol, 10);
    EXPECT_EQ(ann.color, 0xFF0000u);
}

TEST(AnnotationManager, AutoIncrementingIds) {
    AnnotationManager mgr;
    int id1 = mgr.addAnnotation(1, "First");
    int id2 = mgr.addAnnotation(2, "Second");
    int id3 = mgr.addAnnotation(3, "Third");
    EXPECT_EQ(id1, 1);
    EXPECT_EQ(id2, 2);
    EXPECT_EQ(id3, 3);
    EXPECT_EQ(mgr.count(), 3u);
}

TEST(AnnotationManager, RemoveAnnotation) {
    AnnotationManager mgr;
    int id1 = mgr.addAnnotation(1, "First");
    int id2 = mgr.addAnnotation(2, "Second");
    EXPECT_EQ(mgr.count(), 2u);

    EXPECT_TRUE(mgr.removeAnnotation(id1));
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_FALSE(mgr.annotations().count(id1));
    EXPECT_TRUE(mgr.annotations().count(id2));
}

TEST(AnnotationManager, RemoveNonexistent) {
    AnnotationManager mgr;
    mgr.addAnnotation(1, "Note");
    EXPECT_FALSE(mgr.removeAnnotation(999));
    EXPECT_EQ(mgr.count(), 1u);
}

TEST(AnnotationManager, AnnotationsInRange) {
    AnnotationManager mgr;
    mgr.addAnnotation(5, "Row 5");
    mgr.addAnnotation(10, "Row 10");
    mgr.addAnnotation(15, "Row 15");
    mgr.addAnnotation(20, "Row 20");

    auto inRange = mgr.annotationsInRange(8, 17);
    EXPECT_EQ(inRange.size(), 2u);
    // Should contain rows 10 and 15
    bool found10 = false, found15 = false;
    for (const auto* ann : inRange) {
        if (ann->absoluteRow == 10) found10 = true;
        if (ann->absoluteRow == 15) found15 = true;
    }
    EXPECT_TRUE(found10);
    EXPECT_TRUE(found15);
}

TEST(AnnotationManager, AnnotationsInRangeEmpty) {
    AnnotationManager mgr;
    mgr.addAnnotation(5, "Row 5");
    auto inRange = mgr.annotationsInRange(10, 20);
    EXPECT_TRUE(inRange.empty());
}

TEST(AnnotationManager, AnnotationsInRangeInclusive) {
    AnnotationManager mgr;
    mgr.addAnnotation(10, "Row 10");
    mgr.addAnnotation(20, "Row 20");

    // Boundaries should be inclusive
    auto inRange = mgr.annotationsInRange(10, 20);
    EXPECT_EQ(inRange.size(), 2u);
}

TEST(AnnotationManager, HasAnnotation) {
    AnnotationManager mgr;
    mgr.addAnnotation(10, "Note");
    EXPECT_TRUE(mgr.hasAnnotation(10));
    EXPECT_FALSE(mgr.hasAnnotation(11));
}

TEST(AnnotationManager, HasAnnotationAfterRemove) {
    AnnotationManager mgr;
    int id = mgr.addAnnotation(10, "Note");
    EXPECT_TRUE(mgr.hasAnnotation(10));
    mgr.removeAnnotation(id);
    EXPECT_FALSE(mgr.hasAnnotation(10));
}

TEST(AnnotationManager, Clear) {
    AnnotationManager mgr;
    mgr.addAnnotation(1, "A");
    mgr.addAnnotation(2, "B");
    mgr.addAnnotation(3, "C");
    EXPECT_EQ(mgr.count(), 3u);

    mgr.clear();
    EXPECT_EQ(mgr.count(), 0u);
    EXPECT_TRUE(mgr.annotations().empty());
}

TEST(AnnotationManager, ClearResetsIds) {
    AnnotationManager mgr;
    mgr.addAnnotation(1, "A");
    mgr.addAnnotation(2, "B");
    mgr.clear();

    // After clear, IDs restart from 1
    int id = mgr.addAnnotation(1, "New");
    EXPECT_EQ(id, 1);
}

TEST(AnnotationManager, MultipleAnnotationsSameRow) {
    AnnotationManager mgr;
    int id1 = mgr.addAnnotation(10, "First note");
    int id2 = mgr.addAnnotation(10, "Second note");
    EXPECT_NE(id1, id2);
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_TRUE(mgr.hasAnnotation(10));

    auto inRange = mgr.annotationsInRange(10, 10);
    EXPECT_EQ(inRange.size(), 2u);
}

// --- expandBadgeFormat tests ---

TEST(ExpandBadgeFormat, AllPlaceholders) {
    std::string result = expandBadgeFormat(
        "{user}@{hostname}:{cwd} ({shell}) [{branch}]",
        "myhost", "alice", "zsh", "/home/alice", "main");
    EXPECT_EQ(result, "alice@myhost:/home/alice (zsh) [main]");
}

TEST(ExpandBadgeFormat, SinglePlaceholder) {
    EXPECT_EQ(expandBadgeFormat("{hostname}", "server1", "", "", "", ""),
              "server1");
}

TEST(ExpandBadgeFormat, NoPlaceholders) {
    EXPECT_EQ(expandBadgeFormat("Static Badge", "", "", "", "", ""),
              "Static Badge");
}

TEST(ExpandBadgeFormat, EmptyFormat) {
    EXPECT_EQ(expandBadgeFormat("", "host", "user", "sh", "/", "dev"), "");
}

TEST(ExpandBadgeFormat, MissingValues) {
    // Empty values replace placeholders with empty strings
    std::string result = expandBadgeFormat("{user}@{hostname}", "", "", "", "", "");
    EXPECT_EQ(result, "@");
}

TEST(ExpandBadgeFormat, RepeatedPlaceholder) {
    std::string result = expandBadgeFormat(
        "{user}-{user}", "", "bob", "", "", "");
    EXPECT_EQ(result, "bob-bob");
}

TEST(ExpandBadgeFormat, UnknownPlaceholder) {
    // Unknown placeholders are left as-is
    std::string result = expandBadgeFormat("{unknown}", "host", "user", "sh", "/", "main");
    EXPECT_EQ(result, "{unknown}");
}

TEST(TabBadge, DefaultValues) {
    TabBadge badge;
    EXPECT_TRUE(badge.text.empty());
    EXPECT_EQ(badge.bgColor, 0u);
    EXPECT_EQ(badge.fgColor, 0u);
    EXPECT_TRUE(badge.visible);
}
