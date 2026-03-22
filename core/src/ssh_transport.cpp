#include "termcore/ssh_transport.h"

#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace termcore {

// ---------------------------------------------------------------------------
// Home directory expansion
// ---------------------------------------------------------------------------

std::string expandHomePath(const std::string& path) {
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

// ---------------------------------------------------------------------------
// Default identity files
// ---------------------------------------------------------------------------

std::vector<std::string> defaultIdentityFiles() {
    std::vector<std::string> result;
    // Order: prefer ed25519, then rsa, then ecdsa
    const char* names[] = {
        "~/.ssh/id_ed25519",
        "~/.ssh/id_rsa",
        "~/.ssh/id_ecdsa",
    };
    for (const auto& name : names) {
        std::string expanded = expandHomePath(name);
        if (std::filesystem::exists(expanded)) {
            result.push_back(expanded);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Auth method selection
// ---------------------------------------------------------------------------

std::vector<SshAuthMethod> selectAuthMethods(const SshTransportConfig& config) {
    std::vector<SshAuthMethod> methods;

    // 1. Try agent first (most seamless)
    if (config.try_agent) {
        methods.push_back(SshAuthMethod::Agent);
    }

    // 2. Try public key if identity files are configured or defaults exist
    if (!config.identity_files.empty()) {
        methods.push_back(SshAuthMethod::PublicKey);
    } else {
        // Check if any default key exists
        auto defaults = defaultIdentityFiles();
        if (!defaults.empty()) {
            methods.push_back(SshAuthMethod::PublicKey);
        }
    }

    // 3. Try password last
    if (!config.password.empty()) {
        methods.push_back(SshAuthMethod::Password);
    }

    return methods;
}

} // namespace termcore
