#ifndef TERMCORE_PR_DETECTOR_H
#define TERMCORE_PR_DETECTOR_H

#include <chrono>
#include <string>
#include <unordered_map>

namespace termcore {

/// Information about a GitHub pull request associated with the current branch
struct PRInfo {
    int number = 0;
    std::string state; // "OPEN", "MERGED", "CLOSED"
    std::string title;
    std::string url;
    bool found = false;
};

/// Detects GitHub PR status for the current git branch.
/// Caches results with a 30-second TTL to avoid excessive `gh` CLI invocations.
class PRDetector {
public:
    PRDetector() = default;
    ~PRDetector() = default;

    /// Detect PR info for the current branch in the given working directory.
    /// Uses `gh pr view` under the hood; results are cached for 30 seconds.
    PRInfo detectPR(const std::string& working_dir);

    /// Clear the cache, forcing the next detectPR call to re-query.
    void invalidateCache();

private:
    /// Run `gh pr view` and parse the JSON output.
    PRInfo queryGitHubPR(const std::string& working_dir);

    struct CacheEntry {
        PRInfo info;
        std::chrono::steady_clock::time_point expiry;
    };

    std::unordered_map<std::string, CacheEntry> cache_;
};

} // namespace termcore

#endif // TERMCORE_PR_DETECTOR_H
