#ifndef TERMCORE_GIT_BRANCH_DETECTOR_H
#define TERMCORE_GIT_BRANCH_DETECTOR_H

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>

namespace termcore {

/// Detects the current git branch for a given working directory.
/// Caches results with a configurable TTL (default: 2 seconds).
class GitBranchDetector {
public:
    GitBranchDetector() = default;
    ~GitBranchDetector() = default;

    /// Get the current branch name for the given working directory.
    /// Returns empty string if not in a git repo.
    std::string readBranch(const std::string& cwd);

    /// Clear the cache (for testing)
    void clearCache() { cache_.clear(); }

    /// Set the cache TTL in seconds (default: 2).
    void setCacheTtlSeconds(int seconds) {
        cacheTtlSeconds_ = seconds > 0 ? seconds : 1;
    }
    int cacheTtlSeconds() const { return cacheTtlSeconds_; }

    /// Called when the detected branch changes for any cwd.
    /// Signature: void(const std::string& newBranch)
    std::function<void(const std::string&)> onBranchChange;

    /// Optional format override: transforms the branch name before returning it.
    /// Signature: std::string(const std::string& rawBranch)
    std::function<std::string(const std::string&)> formatBranch;

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

    int cacheTtlSeconds_ = 2;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::unordered_map<std::string, std::string> lastBranch_;  // cwd -> last reported branch
};

} // namespace termcore

#endif // TERMCORE_GIT_BRANCH_DETECTOR_H
