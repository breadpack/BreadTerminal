#include <gtest/gtest.h>
#include "termcore/termcore.h"

TEST(TermcoreTest, VersionIsNotNull) {
    const char* version = termcore_version();
    ASSERT_NE(version, nullptr);
}

TEST(TermcoreTest, VersionStringMatches) {
    const char* version = termcore_version();
    EXPECT_STREQ(version, "0.1.0");
}
