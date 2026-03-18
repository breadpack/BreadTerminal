#!/usr/bin/env fish
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
