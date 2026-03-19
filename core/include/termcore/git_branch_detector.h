#ifndef TERMCORE_GIT_BRANCH_DETECTOR_H
#define TERMCORE_GIT_BRANCH_DETECTOR_H

#include <chrono>
#include <string>
#include <unordered_map>
#include <utility>

namespace termcore {

/// Detects the current git branch for a given working directory.
/// Caches results with a 2-second TTL.
class GitBranchDetector {
public:
    GitBranchDetector() = default;
    ~GitBranchDetector() = default;

    /// Get the current branch name for the given working directory.
    /// Returns empty string if not in a git repo.
    std::string readBranch(const std::string& cwd);

    /// Clear the cache (for testing)
    void clearCache() { cache_.clear(); }

private:
    /// Walk up from cwd looking for a directory containing .git/HEAD
    /// Returns path to the .git directory, or empty string if not found.
    std::string findGitDir(const std::string& cwd) const;

    /// Parse .git/HEAD file to extract branch name or short SHA.
    std::string parseHeadFile(const std::string& head_path) const;

    struct CacheEntry {
        std::string branch;
        std::chrono::steady_clock::time_point expiry;
    };

    std::unordered_map<std::string, CacheEntry> cache_;
};

} // namespace termcore

#endif // TERMCORE_GIT_BRANCH_DETECTOR_H
