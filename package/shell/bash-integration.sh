#!/usr/bin/env bash
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
