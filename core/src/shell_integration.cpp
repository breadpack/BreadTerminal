#include "termcore/shell_integration.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#if defined(_WIN32)
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
#define bt_mkdir(p) _mkdir(p)
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#else
#include <sys/stat.h>
#define bt_mkdir(p) mkdir(p, 0755)
#endif

namespace termcore {

// ---------------------------------------------------------------------------
// Shell detection
// ---------------------------------------------------------------------------

ShellType detectShell(const std::string& shell_path) {
    auto endsWith = [](const std::string& str, const std::string& suffix) {
        if (suffix.size() > str.size()) return false;
        return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    if (endsWith(shell_path, "/bash") || shell_path == "bash")
        return ShellType::Bash;
    if (endsWith(shell_path, "/zsh") || shell_path == "zsh")
        return ShellType::Zsh;
    if (endsWith(shell_path, "/fish") || shell_path == "fish")
        return ShellType::Fish;

    return ShellType::Unknown;
}

// ---------------------------------------------------------------------------
// Embedded integration scripts
// ---------------------------------------------------------------------------

static const char* bash_integration_script = R"SCRIPT(
# BreadTerminal bash shell integration
# Injects OSC 133 semantic markers and OSC 7 CWD reporting.

__breadterm_precmd() {
    local exit_code="$?"
    # OSC 133;D — command finished with exit code
    printf '\033]133;D;%s\007' "$exit_code"
    # OSC 7 — report current working directory
    printf '\033]7;file://%s%s\007' "$HOSTNAME" "$PWD"
    # OSC 133;A — prompt start
    printf '\033]133;A\007'
}

__breadterm_preexec() {
    # OSC 133;C — command start (issued just before execution)
    printf '\033]133;C\007'
}

if [[ ! "$PROMPT_COMMAND" == *"__breadterm_precmd"* ]]; then
    PROMPT_COMMAND="__breadterm_precmd${PROMPT_COMMAND:+;$PROMPT_COMMAND}"
fi

trap '__breadterm_preexec' DEBUG

# Emit initial prompt-start marker
printf '\033]133;A\007'

# OSC 133;B — prompt end (inserted via PS1 suffix)
PS1="${PS1}\[\033]133;B\007\]"
)SCRIPT";

static const char* zsh_integration_script = R"SCRIPT(
# BreadTerminal zsh shell integration
# Injects OSC 133 semantic markers and OSC 7 CWD reporting.

autoload -Uz add-zsh-hook

__breadterm_precmd() {
    local exit_code="$?"
    # OSC 133;D — command finished with exit code
    printf '\033]133;D;%s\007' "$exit_code"
    # OSC 7 — report current working directory
    printf '\033]7;file://%s%s\007' "$HOST" "$PWD"
    # OSC 133;A — prompt start
    printf '\033]133;A\007'
}

__breadterm_preexec() {
    # OSC 133;C — command start
    printf '\033]133;C\007'
}

add-zsh-hook precmd __breadterm_precmd
add-zsh-hook preexec __breadterm_preexec

# Emit initial prompt-start marker
printf '\033]133;A\007'

# OSC 133;B — prompt end (inserted via precmd PS1 adjustment)
precmd_functions+=(__breadterm_prompt_end)
__breadterm_prompt_end() {
    PS1="${PS1}%{$(printf '\033]133;B\007')%}"
}
)SCRIPT";

static const char* fish_integration_script = R"SCRIPT(
# BreadTerminal fish shell integration
# Injects OSC 133 semantic markers and OSC 7 CWD reporting.

function __breadterm_fish_prompt --on-event fish_prompt
    # OSC 133;A — prompt start
    printf '\033]133;A\007'
end

function __breadterm_fish_prompt_end --on-event fish_prompt
    # OSC 133;B — prompt end (emitted after prompt rendering)
    printf '\033]133;B\007'
end

function __breadterm_fish_preexec --on-event fish_preexec
    # OSC 133;C — command start
    printf '\033]133;C\007'
end

function __breadterm_fish_postexec --on-event fish_postexec
    # OSC 133;D — command finished with exit code
    printf '\033]133;D;%s\007' $status
    # OSC 7 — report current working directory
    printf '\033]7;file://%s%s\007' (hostname) $PWD
end
)SCRIPT";

std::string getIntegrationScript(ShellType shell) {
    switch (shell) {
    case ShellType::Bash: return bash_integration_script;
    case ShellType::Zsh:  return zsh_integration_script;
    case ShellType::Fish: return fish_integration_script;
    default:              return {};
    }
}

// ---------------------------------------------------------------------------
// Environment variables
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, std::string>> getShellEnvVars() {
    return {
        {"TERM", "xterm-256color"},
        {"BREADTERM_INTEGRATION", "1"},
    };
}

// ---------------------------------------------------------------------------
// Source command generation
// ---------------------------------------------------------------------------

std::string getSourceCommand(ShellType shell, const std::string& scripts_dir) {
    switch (shell) {
    case ShellType::Bash:
        return "source \"" + scripts_dir + "/bash-integration.sh\"";
    case ShellType::Zsh:
        return "source \"" + scripts_dir + "/zsh-integration.zsh\"";
    case ShellType::Fish:
        return "source \"" + scripts_dir + "/fish-integration.fish\"";
    default:
        return {};
    }
}

// ---------------------------------------------------------------------------
// Default scripts directory
// ---------------------------------------------------------------------------

std::string defaultScriptsDir() {
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return std::string(home) + "/.local/share/breadterminal/shell/";
}

// ---------------------------------------------------------------------------
// Install scripts to disk
// ---------------------------------------------------------------------------

static bool mkdirRecursive(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == '/' || i == path.size() - 1) {
            bt_mkdir(current.c_str());
        }
    }
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs) return false;
    ofs << content;
    return ofs.good();
}

bool installScripts(const std::string& dir) {
    if (!mkdirRecursive(dir)) return false;

    std::string sep = (!dir.empty() && dir.back() == '/') ? "" : "/";

    bool ok = true;
    ok = ok && writeFile(dir + sep + "bash-integration.sh",
                         getIntegrationScript(ShellType::Bash));
    ok = ok && writeFile(dir + sep + "zsh-integration.zsh",
                         getIntegrationScript(ShellType::Zsh));
    ok = ok && writeFile(dir + sep + "fish-integration.fish",
                         getIntegrationScript(ShellType::Fish));
    return ok;
}

} // namespace termcore
