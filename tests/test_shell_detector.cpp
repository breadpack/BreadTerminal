#include <gtest/gtest.h>
#include "termcore/profile.h"
#include <set>

using namespace termcore;

TEST(ShellDetectorTest, DetectsAtLeastOneShell) {
    auto profiles = ShellDetector::detect();
    ASSERT_GE(profiles.size(), 1u);
    for (const auto& p : profiles) {
        EXPECT_TRUE(p.auto_detected);
        EXPECT_FALSE(p.id.empty());
        EXPECT_FALSE(p.command.empty());
        EXPECT_FALSE(p.name.empty());
    }
}

#if defined(_WIN32)
TEST(ShellDetectorTest, WindowsDetectsCmd) {
    auto profiles = ShellDetector::detect();
    bool found = false;
    for (const auto& p : profiles) {
        if (p.id == "cmd") { found = true; EXPECT_NE(p.command.find("cmd.exe"), std::string::npos); }
    }
    EXPECT_TRUE(found);
}
#else
TEST(ShellDetectorTest, UnixDetectsDefaultShell) {
    auto profiles = ShellDetector::detect();
    EXPECT_GE(profiles.size(), 1u);
}
#endif

TEST(ShellDetectorTest, NoDuplicateIds) {
    auto profiles = ShellDetector::detect();
    std::set<std::string> ids;
    for (const auto& p : profiles) {
        EXPECT_TRUE(ids.insert(p.id).second) << "Duplicate ID: " << p.id;
    }
}
