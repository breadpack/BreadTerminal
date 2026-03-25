#include "termcore/sidebar_model.h"

#include <gtest/gtest.h>

using namespace termcore;

TEST(SidebarModel, StartsEmpty) {
    AgentTreeTracker tracker;
    SidebarModel model(tracker);
    EXPECT_TRUE(model.entries().empty());
}

TEST(SidebarModel, SetExpandedAndIsExpanded) {
    AgentTreeTracker tracker;
    SidebarModel model(tracker);

    // Default is expanded
    EXPECT_TRUE(model.isExpanded(1));

    model.setExpanded(1, false);
    EXPECT_FALSE(model.isExpanded(1));

    model.setExpanded(1, true);
    EXPECT_TRUE(model.isExpanded(1));
}

TEST(SidebarModel, DifferentPanesIndependentExpansion) {
    AgentTreeTracker tracker;
    SidebarModel model(tracker);

    model.setExpanded(1, false);
    model.setExpanded(2, true);

    EXPECT_FALSE(model.isExpanded(1));
    EXPECT_TRUE(model.isExpanded(2));
    // Pane 3 not set - should default to true
    EXPECT_TRUE(model.isExpanded(3));
}
