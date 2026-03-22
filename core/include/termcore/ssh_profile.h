#ifndef TERMCORE_SSH_PROFILE_H
#define TERMCORE_SSH_PROFILE_H

#include "termcore/ssh_session.h"
#include "termcore/pty.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace termcore {

/// An SSH profile represents a selectable SSH host that can be launched as a
/// terminal tab/pane.  Profiles are discovered from ~/.ssh/config or added
/// manually by the user.
struct SshProfile {
    std::string name;        // Display name (e.g., "prod-server")
    SshConfig config;        // Underlying SSH configuration
    bool auto_terminfo = true;  // Propagate terminfo to remote
};

/// Manages the list of available SSH profiles and creates PTYs for them.
class SshProfileManager {
public:
    /// Discover SSH profiles by parsing the user's ~/.ssh/config.
    /// On Unix: ~/.ssh/config
    /// On Windows: %USERPROFILE%\.ssh\config
    void discoverFromSSHConfig();

    /// Discover from a specific config file path.
    void discoverFromSSHConfig(const std::string& path);

    /// Add a profile manually.
    void addProfile(SshProfile profile);

    /// Remove a profile by name.
    bool removeProfile(const std::string& name);

    /// Get all discovered profiles (read-only).
    const std::vector<SshProfile>& profiles() const { return profiles_; }

    /// Find a profile by name. Returns nullptr if not found.
    const SshProfile* findProfile(const std::string& name) const;

    /// Create a PTY that runs an SSH session for the given profile.
    /// The PTY spawns `ssh` with terminfo propagation if enabled.
    /// @param profile   The SSH profile to connect to.
    /// @param pty       A pre-created PTY instance (from createPty()).
    /// @param rows      Initial terminal rows.
    /// @param cols      Initial terminal cols.
    /// @return true if the PTY was spawned successfully.
    static bool spawnSshPty(const SshProfile& profile,
                            Pty& pty, int rows, int cols);

    /// Build a PtyFactory that creates SSH PTYs for a given profile.
    /// Useful for integration with TabController.
    static std::function<std::unique_ptr<Pty>(int rows, int cols)>
    makeSshPtyFactory(const SshProfile& profile);

    /// Get the default SSH config file path for the current platform.
    static std::string defaultSSHConfigPath();

private:
    std::vector<SshProfile> profiles_;
};

} // namespace termcore

#endif // TERMCORE_SSH_PROFILE_H
