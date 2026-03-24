#ifndef TERMCORE_SHELL_INTEGRATION_H
#define TERMCORE_SHELL_INTEGRATION_H

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace termcore {

enum class ShellType { Bash, Zsh, Fish, Unknown };

ShellType detectShell(const std::string& shell_path);
std::string getIntegrationScript(ShellType shell);
std::vector<std::pair<std::string, std::string>> getShellEnvVars();
std::string getSourceCommand(ShellType shell, const std::string& scripts_dir);
std::string defaultScriptsDir();
bool installScripts(const std::string& dir);

/// Holds Lua-configurable shell integration settings.
/// An instance of this can be owned by the application and passed to
/// LuaShellModule, which populates it via the terminal.shell API.
class ShellIntegrationConfig {
public:
    ShellIntegrationConfig() = default;

    /// Set a custom environment variable to inject into new shell processes.
    void addCustomEnv(const std::string& key, const std::string& value) {
        customEnvVars_[key] = value;
    }

    /// Get all custom env vars set from Lua.
    const std::map<std::string, std::string>& customEnvVars() const {
        return customEnvVars_;
    }

    /// TERM value to advertise over SSH (e.g. "xterm-256color").
    const std::string& sshTerm() const { return sshTerm_; }
    void setSshTerm(const std::string& term) { sshTerm_ = term; }

    /// Called when a shell command finishes.
    /// Signature: void(int exitCode, double durationSeconds)
    std::function<void(int, double)> onCommandFinish;

private:
    std::map<std::string, std::string> customEnvVars_;
    std::string sshTerm_;
};

} // namespace termcore

#endif // TERMCORE_SHELL_INTEGRATION_H
