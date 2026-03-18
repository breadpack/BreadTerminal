#ifndef TERMCORE_SHELL_INTEGRATION_H
#define TERMCORE_SHELL_INTEGRATION_H

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

} // namespace termcore

#endif // TERMCORE_SHELL_INTEGRATION_H
