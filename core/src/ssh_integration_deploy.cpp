#include "termcore/ssh_integration_deploy.h"

#include "termcore/shell_integration.h"
#include "termcore/terminfo.h"

namespace termcore {

// ---------------------------------------------------------------------------
// Shell integration scripts for remote deployment
// ---------------------------------------------------------------------------

std::string SshIntegrationDeploy::bashIntegration() {
    return getIntegrationScript(ShellType::Bash);
}

std::string SshIntegrationDeploy::zshIntegration() {
    return getIntegrationScript(ShellType::Zsh);
}

std::string SshIntegrationDeploy::fishIntegration() {
    return getIntegrationScript(ShellType::Fish);
}

// ---------------------------------------------------------------------------
// Terminfo source
// ---------------------------------------------------------------------------

std::string SshIntegrationDeploy::terminfoSource() {
    return breadTerminalTerminfoSource();
}

// ---------------------------------------------------------------------------
// Remote shell detection command
// ---------------------------------------------------------------------------

std::string SshIntegrationDeploy::detectShellCommand() {
    return R"(basename "$SHELL")";
}

// ---------------------------------------------------------------------------
// Deployment check command
// ---------------------------------------------------------------------------

std::string SshIntegrationDeploy::checkDeployedCommand() {
    return "test -f ~/.config/breadterminal/.deployed-v1 && echo DEPLOYED || echo NOT_DEPLOYED";
}

// ---------------------------------------------------------------------------
// Deploy script generation
// ---------------------------------------------------------------------------

/// Escape single quotes for embedding in a shell heredoc
static std::string escapeForHeredoc(const std::string& s) {
    // Using a heredoc with a unique delimiter avoids most escaping issues.
    // However, if the content itself contains the delimiter, we need a fallback.
    // Since we control the content (our own scripts), this is safe.
    return s;
}

std::string SshIntegrationDeploy::deployScript(const std::string& remoteShell) {
    std::string script;

    // Header
    script += "#!/bin/sh\n";
    script += "# BreadTerminal SSH integration auto-deploy script\n";
    script += "set -e\n\n";

    // Check marker file — skip if already deployed
    script += "MARKER=\"$HOME/.config/breadterminal/.deployed-v1\"\n";
    script += "if [ -f \"$MARKER\" ]; then\n";
    script += "    # Already deployed, just source integration\n";
    script += "    _BT_SKIP_DEPLOY=1\n";
    script += "fi\n\n";

    // Create directories
    script += "if [ -z \"$_BT_SKIP_DEPLOY\" ]; then\n";
    script += "    mkdir -p \"$HOME/.terminfo\"\n";
    script += "    mkdir -p \"$HOME/.config/breadterminal/shell-integration\"\n\n";

    // Write terminfo source and compile with tic
    script += "    # Install terminfo\n";
    script += "    _BT_TI_SRC=\"$HOME/.config/breadterminal/xterm-breadterminal.ti\"\n";
    script += "    cat > \"$_BT_TI_SRC\" << '_BT_TERMINFO_EOF_'\n";
    script += escapeForHeredoc(terminfoSource());
    script += "\n_BT_TERMINFO_EOF_\n\n";

    script += "    if command -v tic > /dev/null 2>&1; then\n";
    script += "        tic -x -o \"$HOME/.terminfo\" \"$_BT_TI_SRC\" 2>/dev/null && "
              "_BT_TIC_OK=1 || true\n";
    script += "    fi\n\n";

    // Write bash integration script
    script += "    # Install bash integration\n";
    script += "    cat > \"$HOME/.config/breadterminal/shell-integration/bash-integration.sh\""
              " << '_BT_BASH_EOF_'\n";
    script += escapeForHeredoc(bashIntegration());
    script += "\n_BT_BASH_EOF_\n\n";

    // Write zsh integration script
    script += "    # Install zsh integration\n";
    script += "    cat > \"$HOME/.config/breadterminal/shell-integration/zsh-integration.zsh\""
              " << '_BT_ZSH_EOF_'\n";
    script += escapeForHeredoc(zshIntegration());
    script += "\n_BT_ZSH_EOF_\n\n";

    // Write fish integration script
    script += "    # Install fish integration\n";
    script += "    cat > \"$HOME/.config/breadterminal/shell-integration/fish-integration.fish\""
              " << '_BT_FISH_EOF_'\n";
    script += escapeForHeredoc(fishIntegration());
    script += "\n_BT_FISH_EOF_\n\n";

    // Create marker file
    script += "    # Mark as deployed\n";
    script += "    echo \"v1\" > \"$MARKER\"\n";
    script += "fi\n\n";

    // Set TERM if terminfo was compiled successfully
    script += "if [ -f \"$HOME/.terminfo/x/xterm-breadterminal\" ] || "
              "[ -f \"$HOME/.terminfo/78/xterm-breadterminal\" ]; then\n";
    script += "    export TERM=xterm-breadterminal\n";
    script += "    export TERMINFO=\"$HOME/.terminfo\"\n";
    script += "fi\n\n";

    // Detect shell and source appropriate integration
    script += "# Source shell integration\n";
    script += "_BT_INTEGRATION_DIR=\"$HOME/.config/breadterminal/shell-integration\"\n";

    if (!remoteShell.empty()) {
        // Shell explicitly specified
        script += "_BT_SHELL=\"" + remoteShell + "\"\n";
    } else {
        script += "_BT_SHELL=$(" + detectShellCommand() + ")\n";
    }

    script += "case \"$_BT_SHELL\" in\n";
    script += "    bash)\n";
    script += "        . \"$_BT_INTEGRATION_DIR/bash-integration.sh\"\n";
    script += "        ;;\n";
    script += "    zsh)\n";
    script += "        . \"$_BT_INTEGRATION_DIR/zsh-integration.zsh\"\n";
    script += "        ;;\n";
    script += "    fish)\n";
    script += "        . \"$_BT_INTEGRATION_DIR/fish-integration.fish\"\n";
    script += "        ;;\n";
    script += "esac\n";

    return script;
}

} // namespace termcore
