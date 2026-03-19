#include "termcore/pr_detector.h"

#include <array>
#include <cstdio>
#include <sstream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace termcore {

namespace {

/// Run a command and capture its stdout. Returns empty string on failure.
std::string runCommand(const std::string& cmd) {
    std::string result;

#if defined(_WIN32)
    // Use _popen on Windows
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif

    if (!pipe) {
        return result;
    }

    std::array<char, 256> buffer;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }

#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return result;
}

/// Simple JSON string value extraction (avoids dependency on a JSON library
/// for this small use case). Looks for "key": "value" patterns.
std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        // Try with space before colon
        search = "\"" + key + "\" :";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
    }

    pos = json.find('"', pos + search.size());
    if (pos == std::string::npos) return "";
    pos++; // skip opening quote

    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";

    return json.substr(pos, end - pos);
}

/// Extract a JSON integer value for the given key.
int extractJsonInt(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\" :";
        pos = json.find(search);
        if (pos == std::string::npos) return 0;
    }

    pos += search.size();
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    if (pos >= json.size()) return 0;

    try {
        return std::stoi(json.substr(pos));
    } catch (...) {
        return 0;
    }
}

} // anonymous namespace

PRInfo PRDetector::detectPR(const std::string& working_dir) {
    if (working_dir.empty()) {
        return {};
    }

    auto now = std::chrono::steady_clock::now();

    // Check cache
    auto it = cache_.find(working_dir);
    if (it != cache_.end() && now < it->second.expiry) {
        return it->second.info;
    }

    // Query GitHub
    PRInfo info = queryGitHubPR(working_dir);

    // Cache with 30s TTL
    cache_[working_dir] = {info, now + std::chrono::seconds(30)};
    return info;
}

void PRDetector::invalidateCache() {
    cache_.clear();
}

PRInfo PRDetector::queryGitHubPR(const std::string& working_dir) {
    PRInfo info;

    // Build command: gh pr view --json number,state,title,url
    // Run in the specified working directory
    std::string cmd;
#if defined(_WIN32)
    cmd = "cd /d \"" + working_dir + "\" && gh pr view --json number,state,title,url 2>nul";
#else
    cmd = "cd \"" + working_dir + "\" && gh pr view --json number,state,title,url 2>/dev/null";
#endif

    std::string output = runCommand(cmd);
    if (output.empty()) {
        return info;
    }

    // Parse JSON response
    info.number = extractJsonInt(output, "number");
    info.state = extractJsonString(output, "state");
    info.title = extractJsonString(output, "title");
    info.url = extractJsonString(output, "url");
    info.found = (info.number > 0);

    return info;
}

} // namespace termcore
