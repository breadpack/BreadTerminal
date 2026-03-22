#if !defined(_WIN32)

#include "termcore/profile.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace termcore {

static Profile makeProfile(const std::string& id, const std::string& name,
                            const std::string& command, const std::string& icon) {
    Profile p;
    p.id = id; p.name = name; p.command = command; p.icon = icon;
    p.auto_detected = true;
    return p;
}

static std::string shellNameFromPath(const std::string& path) {
    auto pos = path.rfind('/');
    return (pos != std::string::npos) ? path.substr(pos + 1) : path;
}

static std::string friendlyName(const std::string& bin) {
    if (bin == "bash") return "Bash";
    if (bin == "zsh") return "Zsh";
    if (bin == "fish") return "Fish";
    if (bin == "sh") return "sh";
    if (bin == "dash") return "Dash";
    if (bin == "tcsh") return "tcsh";
    if (bin == "csh") return "csh";
    if (bin == "ksh") return "KornShell";
    if (bin == "nu") return "Nushell";
    if (bin == "elvish") return "Elvish";
    if (bin == "xonsh") return "xonsh";
    if (bin == "pwsh") return "PowerShell";
    return bin;
}

std::vector<Profile> detectUnixShells() {
    std::vector<Profile> profiles;
    std::set<std::string> seen;

    std::ifstream ifs("/etc/shells");
    if (ifs) {
        std::string line;
        while (std::getline(ifs, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] != '/') continue;
            if (!fs::exists(line)) continue;
            std::string bin = shellNameFromPath(line);
            if (bin.empty() || !seen.insert(bin).second) continue;
            profiles.push_back(makeProfile(bin, friendlyName(bin), line, bin));
        }
    }

    if (profiles.empty()) {
        const char* sh = std::getenv("SHELL");
        if (sh && sh[0]) {
            std::string bin = shellNameFromPath(sh);
            profiles.push_back(makeProfile(bin, friendlyName(bin), sh, bin));
        } else {
            profiles.push_back(makeProfile("sh", "sh", "/bin/sh", "sh"));
        }
    }

    const char* extras[] = {"nu", "elvish", "xonsh"};
    for (const char* name : extras) {
        if (seen.count(name)) continue;
        std::string paths[] = {
            std::string("/usr/local/bin/") + name,
            std::string("/usr/bin/") + name,
            std::string("/opt/homebrew/bin/") + name,
        };
        for (const auto& path : paths) {
            if (fs::exists(path)) {
                profiles.push_back(makeProfile(name, friendlyName(name), path, name));
                break;
            }
        }
    }

    return profiles;
}

} // namespace termcore
#endif
