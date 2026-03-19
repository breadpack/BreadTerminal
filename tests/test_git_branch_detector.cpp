#include "termcore/git_branch_detector.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

class GitBranchDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test repos
        tmp_dir_ = fs::temp_directory_path() / ("git_branch_test_" + std::to_string(getpid()));
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override {
        fs::remove_all(tmp_dir_);
    }

    // Create a fake .git directory with a HEAD file
    void createFakeGitRepo(const fs::path& dir, const std::string& head_content) {
        fs::create_directories(dir / ".git");
        std::ofstream f(dir / ".git" / "HEAD");
        f << head_content;
    }

    fs::path tmp_dir_;
};

TEST_F(GitBranchDetectorTest, ReadsBranchFromSymbolicRef) {
    createFakeGitRepo(tmp_dir_, "ref: refs/heads/main\n");

    termcore::GitBranchDetector detector;
    std::string branch = detector.readBranch(tmp_dir_.string());
    EXPECT_EQ(branch, "main");
}

TEST_F(GitBranchDetectorTest, ReadsBranchWithSlashes) {
    createFakeGitRepo(tmp_dir_, "ref: refs/heads/feature/sidebar-impl\n");

    termcore::GitBranchDetector detector;
    std::string branch = detector.readBranch(tmp_dir_.string());
    EXPECT_EQ(branch, "feature/sidebar-impl");
}

TEST_F(GitBranchDetectorTest, DetachedHeadReturnsShortSHA) {
    createFakeGitRepo(tmp_dir_, "abc1234def5678901234567890abcdef01234567\n");

    termcore::GitBranchDetector detector;
    std::string branch = detector.readBranch(tmp_dir_.string());
    EXPECT_EQ(branch, "abc1234");
}

TEST_F(GitBranchDetectorTest, NonGitDirReturnsEmpty) {
    // tmp_dir_ exists but has no .git
    termcore::GitBranchDetector detector;
    std::string branch = detector.readBranch(tmp_dir_.string());
    EXPECT_EQ(branch, "");
}

TEST_F(GitBranchDetectorTest, EmptyCwdReturnsEmpty) {
    termcore::GitBranchDetector detector;
    EXPECT_EQ(detector.readBranch(""), "");
}

TEST_F(GitBranchDetectorTest, WalksUpDirectoryTree) {
    // Create git repo in parent, test from subdirectory
    createFakeGitRepo(tmp_dir_, "ref: refs/heads/develop\n");
    auto sub = tmp_dir_ / "a" / "b" / "c";
    fs::create_directories(sub);

    termcore::GitBranchDetector detector;
    std::string branch = detector.readBranch(sub.string());
    EXPECT_EQ(branch, "develop");
}

TEST_F(GitBranchDetectorTest, CacheReturnsCachedValue) {
    createFakeGitRepo(tmp_dir_, "ref: refs/heads/main\n");

    termcore::GitBranchDetector detector;
    EXPECT_EQ(detector.readBranch(tmp_dir_.string()), "main");

    // Change HEAD file - should still return cached value
    {
        std::ofstream f(tmp_dir_ / ".git" / "HEAD");
        f << "ref: refs/heads/other\n";
    }

    // Cache hasn't expired yet
    EXPECT_EQ(detector.readBranch(tmp_dir_.string()), "main");
}

TEST_F(GitBranchDetectorTest, CacheExpiresAfterTTL) {
    createFakeGitRepo(tmp_dir_, "ref: refs/heads/main\n");

    termcore::GitBranchDetector detector;
    EXPECT_EQ(detector.readBranch(tmp_dir_.string()), "main");

    // Change HEAD file
    {
        std::ofstream f(tmp_dir_ / ".git" / "HEAD");
        f << "ref: refs/heads/other\n";
    }

    // Wait for cache to expire (2s TTL + margin)
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));

    EXPECT_EQ(detector.readBranch(tmp_dir_.string()), "other");
}

TEST_F(GitBranchDetectorTest, ClearCacheForcesFreshRead) {
    createFakeGitRepo(tmp_dir_, "ref: refs/heads/main\n");

    termcore::GitBranchDetector detector;
    EXPECT_EQ(detector.readBranch(tmp_dir_.string()), "main");

    // Change HEAD and clear cache
    {
        std::ofstream f(tmp_dir_ / ".git" / "HEAD");
        f << "ref: refs/heads/other\n";
    }
    detector.clearCache();

    EXPECT_EQ(detector.readBranch(tmp_dir_.string()), "other");
}
