#include "termcore/git_branch_detector.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

#if defined(_WIN32)
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#endif

namespace termcore {

static bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool dirExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string GitBranchDetector::readBranch(const std::string& cwd) {
    if (cwd.empty()) return "";

    auto now = std::chrono::steady_clock::now();

    // Check cache
    auto it = cache_.find(cwd);
    if (it != cache_.end() && now < it->second.expiry) {
        return it->second.branch;
    }

    // Find .git dir and parse
    std::string git_dir = findGitDir(cwd);
    std::string branch;
    if (!git_dir.empty()) {
        std::string head_path = git_dir + "/HEAD";
        branch = parseHeadFile(head_path);
    }

    // Cache with 2s TTL
    cache_[cwd] = {branch, now + std::chrono::seconds(2)};
    return branch;
}

std::string GitBranchDetector::findGitDir(const std::string& cwd) const {
    std::string dir = cwd;

    // Walk up the directory tree
    while (!dir.empty()) {
        std::string git_dir = dir + "/.git";

        // Check if .git/HEAD exists (normal repo)
        if (fileExists(git_dir + "/HEAD")) {
            return git_dir;
        }

        // Check if .git is a file (worktree: contains "gitdir: <path>")
        if (fileExists(git_dir)) {
            std::ifstream f(git_dir);
            std::string line;
            if (std::getline(f, line)) {
                const std::string prefix = "gitdir: ";
                if (line.substr(0, prefix.size()) == prefix) {
                    std::string linked_dir = line.substr(prefix.size());
                    // Trim trailing whitespace
                    while (!linked_dir.empty() &&
                           (linked_dir.back() == '\n' || linked_dir.back() == '\r' ||
                            linked_dir.back() == ' ')) {
                        linked_dir.pop_back();
                    }
                    // Resolve relative path
#if defined(_WIN32)
                    bool is_absolute = (linked_dir.size() >= 2 && linked_dir[1] == ':')
                                    || (!linked_dir.empty() && (linked_dir[0] == '/' || linked_dir[0] == '\\'));
#else
                    bool is_absolute = !linked_dir.empty() && linked_dir[0] == '/';
#endif
                    if (!is_absolute) {
                        linked_dir = dir + "/" + linked_dir;
                    }
                    if (fileExists(linked_dir + "/HEAD")) {
                        return linked_dir;
                    }
                }
            }
        }

        // Move to parent directory
#if defined(_WIN32)
        auto pos = dir.find_last_of("/\\");
#else
        auto pos = dir.find_last_of('/');
#endif
        if (pos == std::string::npos) {
            break;
        }
#if defined(_WIN32)
        // Windows drive root: "C:\" — stop after checking it
        if (pos <= 2 && dir.size() >= 2 && dir[1] == ':') {
            if (dir.size() > 3) {
                dir = dir.substr(0, 3); // "C:\"
                continue;
            }
            break;
        }
#else
        // Unix root "/"
        if (pos == 0) {
            if (dir.size() > 1) {
                dir = "/";
                continue;
            }
            break;
        }
#endif
        dir = dir.substr(0, pos);
    }

    return "";
}

std::string GitBranchDetector::parseHeadFile(const std::string& head_path) const {
    std::ifstream f(head_path);
    if (!f.is_open()) return "";

    std::string line;
    if (!std::getline(f, line)) return "";

    // Trim trailing whitespace
    while (!line.empty() &&
           (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }

    // Symbolic ref: "ref: refs/heads/branch-name"
    const std::string ref_prefix = "ref: refs/heads/";
    if (line.substr(0, ref_prefix.size()) == ref_prefix) {
        return line.substr(ref_prefix.size());
    }

    // Detached HEAD: return short SHA (first 7 chars)
    if (line.size() >= 7) {
        return line.substr(0, 7);
    }

    return line;
}

} // namespace termcore
