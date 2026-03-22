#include "termcore/ssh_session.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <glob.h>
#include <pwd.h>
#include <unistd.h>
#endif

namespace termcore {

// ---------------------------------------------------------------------------
// SshConfig helpers
// ---------------------------------------------------------------------------

std::string SshConfig::displayName() const {
    std::string name;
    if (!user.empty()) {
        name = user + "@";
    }
    name += hostname.empty() ? host : hostname;
    if (port != 22) {
        name += ":" + std::to_string(port);
    }
    return name;
}

// ---------------------------------------------------------------------------
// Pattern matching
// ---------------------------------------------------------------------------

bool SshSession::matchHostPattern(const std::string& pattern,
                                  const std::string& hostname) {
    size_t pi = 0, hi = 0;
    size_t star_p = std::string::npos, star_h = 0;

    while (hi < hostname.size()) {
        if (pi < pattern.size() &&
            (pattern[pi] == '?' || pattern[pi] == hostname[hi])) {
            ++pi;
            ++hi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_p = pi++;
            star_h = hi;
        } else if (star_p != std::string::npos) {
            pi = star_p + 1;
            hi = ++star_h;
        } else {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }
    return pi == pattern.size();
}

// ---------------------------------------------------------------------------
// SSH config parser
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

static std::string expandTilde(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;

    std::string home;
#if defined(_WIN32)
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) home = userprofile;
#else
    const char* h = std::getenv("HOME");
    if (h) {
        home = h;
    } else {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
#endif

    if (home.empty()) return path;

    if (path.size() == 1) return home;
    if (path[1] == '/' || path[1] == '\\') {
        return home + path.substr(1);
    }
    return path;
}

static std::vector<std::string> resolveIncludePaths(const std::string& pattern,
                                                     const std::string& config_dir) {
    std::string expanded = expandTilde(pattern);

    if (!expanded.empty() && expanded[0] != '/'
#if defined(_WIN32)
        && !(expanded.size() >= 2 && expanded[1] == ':')
#endif
    ) {
        expanded = config_dir + "/" + expanded;
    }

    std::vector<std::string> results;

#if !defined(_WIN32)
    glob_t globbuf{};
    if (glob(expanded.c_str(), GLOB_TILDE | GLOB_NOCHECK, nullptr, &globbuf) == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
            results.emplace_back(globbuf.gl_pathv[i]);
        }
        globfree(&globbuf);
    }
#else
    results.push_back(expanded);
#endif

    return results;
}

static bool isWildcardOnly(const std::string& pattern) {
    for (char c : pattern) {
        if (c != '*' && c != ' ' && c != '\t') return false;
    }
    return true;
}

static bool parseConfigLine(const std::string& line,
                            std::string& keyword, std::string& value) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') return false;

    auto sep = trimmed.find_first_of(" \t=");
    if (sep == std::string::npos) return false;

    keyword = trimmed.substr(0, sep);
    auto val_start = trimmed.find_first_not_of(" \t=", sep);
    if (val_start == std::string::npos) {
        value.clear();
    } else {
        value = trimmed.substr(val_start);
    }
    return true;
}

void SshSession::parseSSHConfigFile(const std::string& path,
                                    std::vector<SshConfig>& configs,
                                    int depth) {
    if (depth > 10) return;

    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string config_dir;
    auto last_sep = path.find_last_of("/\\");
    if (last_sep != std::string::npos) {
        config_dir = path.substr(0, last_sep);
    } else {
        config_dir = ".";
    }

    SshConfig* current = nullptr;
    std::string line;

    while (std::getline(file, line)) {
        std::string keyword, value;
        if (!parseConfigLine(line, keyword, value)) continue;

        if (iequals(keyword, "Host")) {
            std::string pattern = trim(value);

            if (isWildcardOnly(pattern)) {
                current = nullptr;
                continue;
            }

            configs.emplace_back();
            current = &configs.back();
            current->host = pattern;
        } else if (iequals(keyword, "Match")) {
            current = nullptr;
        } else if (iequals(keyword, "Include")) {
            auto paths = resolveIncludePaths(value, config_dir);
            for (const auto& p : paths) {
                parseSSHConfigFile(p, configs, depth + 1);
            }
        } else if (current) {
            if (iequals(keyword, "HostName")) {
                current->hostname = value;
            } else if (iequals(keyword, "Port")) {
                try { current->port = std::stoi(value); }
                catch (...) {}
            } else if (iequals(keyword, "User")) {
                current->user = value;
            } else if (iequals(keyword, "IdentityFile")) {
                current->identity_file = expandTilde(value);
            } else if (iequals(keyword, "ForwardAgent")) {
                current->forward_agent = iequals(value, "yes");
            } else if (iequals(keyword, "ProxyCommand")) {
                current->proxy_command = value;
            } else if (iequals(keyword, "ProxyJump")) {
                current->proxy_jump = value;
            }
        }
    }
}

std::vector<SshConfig> SshSession::parseSSHConfig(const std::string& path) {
    std::vector<SshConfig> configs;
    std::string expanded = expandTilde(path);
    parseSSHConfigFile(expanded, configs, 0);
    return configs;
}

// ---------------------------------------------------------------------------
// Command building
// ---------------------------------------------------------------------------

std::string SshSession::buildSshCommand(const SshConfig& config) {
    auto args = buildSshArgs(config);
    std::string cmd;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) cmd += ' ';
        bool needs_quote = args[i].find(' ') != std::string::npos;
        if (needs_quote) cmd += '"';
        cmd += args[i];
        if (needs_quote) cmd += '"';
    }
    return cmd;
}

std::vector<std::string> SshSession::buildSshArgs(const SshConfig& config) {
    std::vector<std::string> args;
    args.push_back("ssh");

    if (config.port != 22) {
        args.push_back("-p");
        args.push_back(std::to_string(config.port));
    }

    if (!config.identity_file.empty()) {
        args.push_back("-i");
        args.push_back(config.identity_file);
    }

    if (config.forward_agent) {
        args.push_back("-A");
    }

    if (!config.proxy_command.empty()) {
        args.push_back("-o");
        args.push_back("ProxyCommand=" + config.proxy_command);
    }

    if (!config.proxy_jump.empty()) {
        args.push_back("-J");
        args.push_back(config.proxy_jump);
    }

    std::string dest;
    if (!config.user.empty()) {
        dest = config.user + "@";
    }
    dest += config.hostname.empty() ? config.host : config.hostname;
    args.push_back(dest);

    return args;
}

// ---------------------------------------------------------------------------
// Host lookup
// ---------------------------------------------------------------------------

const SshConfig* SshSession::findHost(const std::vector<SshConfig>& configs,
                                      const std::string& host) {
    for (const auto& c : configs) {
        if (c.host == host) return &c;
    }

    for (const auto& c : configs) {
        if (matchHostPattern(c.host, host)) return &c;
    }

    return nullptr;
}

} // namespace termcore
