#!/usr/bin/env zsh
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
