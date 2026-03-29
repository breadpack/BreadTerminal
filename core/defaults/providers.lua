-- BreadTerminal default AI CLI provider definitions
-- Users can add custom providers in config.lua with terminal.provider()

terminal.provider("claude_code", {
    display_name = "Claude Code",
    agent_type = "ClaudeCode",
    detect_process = {"claude"},
    detect_env = {"CLAUDE_CODE_SESSION"},
    hooks = {
        config_dir = "~/.claude",
        settings_file = "settings.json",
        settings_format = "json",
        events = {
            {
                bread_event = "SubagentStart",
                hook_name = "SubagentStart",
                env_map = {
                    agent_id = "CLAUDE_AGENT_ID",
                    agent_type = "CLAUDE_AGENT_TYPE",
                    description = "CLAUDE_AGENT_DESCRIPTION",
                },
            },
            {
                bread_event = "SubagentStop",
                hook_name = "SubagentStop",
                env_map = { agent_id = "CLAUDE_AGENT_ID" },
            },
            {
                bread_event = "Notification",
                hook_name = "Notification",
                env_map = { body = "CLAUDE_NOTIFICATION_MESSAGE" },
            },
            {
                bread_event = "PostToolUse",
                hook_name = "PostToolUse",
                env_map = { tool_name = "CLAUDE_TOOL_NAME" },
            },
        },
    },
})

terminal.provider("codex", {
    display_name = "Codex",
    agent_type = "Codex",
    detect_process = {"codex"},
    detect_env = {"CODEX_SESSION"},
    hooks = {
        config_dir = "~/.codex",
        settings_file = "config.json",
        settings_format = "json",
        events = {},
    },
})

terminal.provider("gemini_cli", {
    display_name = "Gemini CLI",
    agent_type = "GeminiCli",
    detect_process = {"gemini"},
    detect_env = {"GEMINI_CLI"},
    hooks = {
        config_dir = "~/.gemini",
        settings_file = "settings.json",
        settings_format = "json",
        events = {},
    },
})

terminal.provider("aider", {
    display_name = "Aider",
    agent_type = "Aider",
    detect_process = {"aider"},
    detect_env = {},
})

terminal.provider("opencode", {
    display_name = "OpenCode",
    agent_type = "OpenCode",
    detect_process = {"opencode"},
    detect_env = {},
})

terminal.provider("goose", {
    display_name = "Goose",
    agent_type = "Goose",
    detect_process = {"goose"},
    detect_env = {},
})

terminal.provider("amp", {
    display_name = "Amp",
    agent_type = "Amp",
    detect_process = {"amp"},
    detect_env = {},
})

terminal.provider("cline", {
    display_name = "Cline",
    agent_type = "Cline",
    detect_process = {"cline"},
    detect_env = {},
})

-- Auto-install hooks when a provider with hook config is detected
terminal.hooks.on_provider_detected(function(data)
    if not terminal.hooks.is_installed(data.provider_id) then
        terminal.hooks.install(data.provider_id)
    end
end)
