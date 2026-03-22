#include "termcore/ssh_profile.h"
#include "termcore/ssh_terminfo.h"

#include <algorithm>
#include <cstdlib>

namespace termcore {

// ---------------------------------------------------------------------------
// Default SSH config path
// ---------------------------------------------------------------------------

std::string SshProfileManager::defaultSSHConfigPath() {
#if defined(_WIN32)
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) return std::string(userprofile) + "\\.ssh\\config";
    return {};
#else
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/.ssh/config";
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

void SshProfileManager::discoverFromSSHConfig() {
    std::string path = defaultSSHConfigPath();
    if (!path.empty()) {
        discoverFromSSHConfig(path);
    }
}

void SshProfileManager::discoverFromSSHConfig(const std::string& path) {
    auto configs = SshSession::parseSSHConfig(path);

    for (auto& cfg : configs) {
        // Skip if we already have a profile with this host name
        bool exists = false;
        for (const auto& p : profiles_) {
            if (p.config.host == cfg.host) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        SshProfile profile;
        profile.name = cfg.host;
        profile.config = std::move(cfg);
        profiles_.push_back(std::move(profile));
    }
}

// ---------------------------------------------------------------------------
// Profile management
// ---------------------------------------------------------------------------

void SshProfileManager::addProfile(SshProfile profile) {
    // Replace existing profile with the same name
    for (auto& p : profiles_) {
        if (p.name == profile.name) {
            p = std::move(profile);
            return;
        }
    }
    profiles_.push_back(std::move(profile));
}

bool SshProfileManager::removeProfile(const std::string& name) {
    auto it = std::remove_if(profiles_.begin(), profiles_.end(),
                             [&](const SshProfile& p) { return p.name == name; });
    if (it == profiles_.end()) return false;
    profiles_.erase(it, profiles_.end());
    return true;
}

const SshProfile* SshProfileManager::findProfile(const std::string& name) const {
    for (const auto& p : profiles_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// PTY spawning
// ---------------------------------------------------------------------------

bool SshProfileManager::spawnSshPty(const SshProfile& profile,
                                     Pty& pty, int rows, int cols) {
    auto baseArgs = SshSession::buildSshArgs(profile.config);

    std::vector<std::string> args;
    if (profile.auto_terminfo) {
        args = SshTerminfoHelper::wrapSshArgs(baseArgs);
    } else {
        args = baseArgs;
    }

    // First element is the command ("ssh"), rest are arguments
    std::string command = args[0];
    std::vector<std::string> cmdArgs(args.begin() + 1, args.end());

    // Set BreadTerminal environment variables for the local ssh process
    std::vector<std::pair<std::string, std::string>> env_vars = {
        {"TERM_PROGRAM", "BreadTerminal"},
        {"BREADTERMINAL", "1"},
    };

    return pty.spawn(command, cmdArgs, "", rows, cols, env_vars);
}

std::function<std::unique_ptr<Pty>(int rows, int cols)>
SshProfileManager::makeSshPtyFactory(const SshProfile& profile) {
    // Capture profile by value so the factory is self-contained
    SshProfile captured = profile;
    return [captured](int rows, int cols) -> std::unique_ptr<Pty> {
        auto pty = createPty();
        if (!pty) return nullptr;
        if (!spawnSshPty(captured, *pty, rows, cols)) return nullptr;
        return pty;
    };
}

} // namespace termcore
