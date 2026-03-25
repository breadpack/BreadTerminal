# Multi-Provider AI CLI Integration Design

## Goal

Replace BreadTerminal's Claude Code-only hook integration with a universal provider system that supports any AI CLI tool (Claude Code, Codex, Gemini CLI, Aider, Goose, Amp, OpenCode, Cline, and user-defined tools) through three communication channels: hook scripts, OSC 7770 in-band protocol, and direct CLI calls.

## Architecture

```
┌─────────────────┐
│   AI CLI Tool    │
└──┬─────┬─────┬──┘
   │     │     │
   │     │     └─── OSC 7770 (stdout printf)
   │     │              ↓
   │     │         Screen OSC parser (screen_osc.cpp)
   │     │              ↓
   │     └──── bread hook-event --json (hook script → CLI → named pipe)
   │                 ↓
   │            HookBridge::processHookEvent()
   │                 ↓
   └────── bread CLI (pane.split, set-status, etc.)
                ↓
           JSON-RPC Server
                ↓
     ┌──────────────────────┐
     │   AgentTreeTracker    │
     │   AgentTracker        │
     │   NotificationStore   │
     └──────┬───────────────┘
            ↓
      SidebarModel → UI
```

All three channels converge to the same internal event pipeline. The HookBridge JSON event format is the universal protocol:

```json
{"event":"SubagentStart","agent_id":"...","agent_type":"...","pane_id":3}
{"event":"SubagentStop","agent_id":"...","pane_id":3}
{"event":"StateChange","agent_id":"...","state":"thinking","pane_id":3}
{"event":"Notification","title":"...","body":"...","urgency":"normal","pane_id":3}
```

## Component 1: Provider Registry

### ProviderInfo struct

```cpp
struct ProviderHookEvent {
    std::string bread_event;     // e.g. "SubagentStart"
    std::string hook_name;       // e.g. "subagent-start" (tool's hook event name)
    // Maps BreadTerminal field → tool's environment variable
    // e.g. {"agent_id" → "CLAUDE_AGENT_ID"}
    std::vector<std::pair<std::string, std::string>> env_map;
};

struct ProviderHooksConfig {
    std::string config_dir;       // e.g. "~/.claude"
    std::string settings_file;    // e.g. "settings.json"
    std::string settings_format;  // "json", "yaml", "toml"
    std::vector<ProviderHookEvent> events;
};

struct ProviderInfo {
    std::string id;                // e.g. "claude_code"
    std::string display_name;     // e.g. "Claude Code"
    std::string agent_type;       // maps to AgentType enum
    std::vector<std::string> detect_process;  // process name substrings
    std::vector<std::string> detect_env;      // env var markers
    ProviderHooksConfig hooks;    // empty if tool has no hook system
    bool hooks_installed = false; // runtime state, not persisted in Lua
};
```

### ProviderRegistry class

```cpp
class ProviderRegistry {
public:
    /// Register a provider (called from Lua binding)
    void registerProvider(ProviderInfo info);

    /// Find provider by agent type (called from AgentTracker on detection)
    const ProviderInfo* findByAgentType(const std::string& agent_type) const;

    /// Find provider by process name or env vars
    const ProviderInfo* detect(const std::string& process_name,
                               const std::vector<std::string>& env_vars) const;

    /// Get all registered providers
    const std::vector<ProviderInfo>& all() const;

    /// Mark a provider's hooks as installed
    void markInstalled(const std::string& provider_id);

    /// Check if hooks are installed for a provider
    bool isInstalled(const std::string& provider_id) const;

private:
    std::vector<ProviderInfo> providers_;
    std::set<std::string> installed_;  // persisted via config
};
```

### Lua binding

```lua
-- defaults/providers.lua
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
                hook_name = "subagent-start",
                env_map = {
                    agent_id = "CLAUDE_AGENT_ID",
                    agent_type = "CLAUDE_AGENT_TYPE",
                    description = "CLAUDE_AGENT_DESCRIPTION",
                },
            },
            {
                bread_event = "SubagentStop",
                hook_name = "subagent-stop",
                env_map = { agent_id = "CLAUDE_AGENT_ID" },
            },
            {
                bread_event = "Notification",
                hook_name = "notification",
                env_map = { body = "CLAUDE_NOTIFICATION_MESSAGE" },
            },
            {
                bread_event = "PostTool",
                hook_name = "post-tool",
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
        events = {},  -- Codex hook event names TBD
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
        events = {},  -- Gemini hook event names TBD
    },
})

terminal.provider("aider", {
    display_name = "Aider",
    agent_type = "Aider",
    detect_process = {"aider"},
    detect_env = {},
    hooks = {},  -- Aider has no hook system; OSC channel only
})

terminal.provider("opencode", {
    display_name = "OpenCode",
    agent_type = "OpenCode",
    detect_process = {"opencode"},
    detect_env = {},
    hooks = {},
})

terminal.provider("goose", {
    display_name = "Goose",
    agent_type = "Goose",
    detect_process = {"goose"},
    detect_env = {},
    hooks = {},
})

terminal.provider("amp", {
    display_name = "Amp",
    agent_type = "Amp",
    detect_process = {"amp"},
    detect_env = {},
    hooks = {},
})

terminal.provider("cline", {
    display_name = "Cline",
    agent_type = "Cline",
    detect_process = {"cline"},
    detect_env = {},
    hooks = {},
})
```

Users can add custom providers in their config.lua:

```lua
terminal.provider("my_agent", {
    display_name = "My Agent",
    agent_type = "Custom",
    detect_process = {"myagent"},
    hooks = {
        config_dir = "~/.myagent",
        settings_file = "config.json",
        settings_format = "json",
        events = {
            {
                bread_event = "Notification",
                hook_name = "on-complete",
                env_map = { body = "MY_AGENT_MESSAGE" },
            },
        },
    },
})
```

## Component 2: OSC 7770 Protocol

### Format

```
ESC ] 7770 ; <JSON> ST
```

Where `<JSON>` is the same HookBridge event format. The `pane_id` field is optional — if omitted, the terminal injects the pane ID of the PTY that emitted the sequence.

### Examples

```bash
# State change
printf '\e]7770;{"event":"StateChange","state":"thinking"}\e\\'

# Notification
printf '\e]7770;{"event":"Notification","title":"Done","body":"Build complete"}\e\\'

# Subagent start
printf '\e]7770;{"event":"SubagentStart","agent_id":"a1","agent_type":"worker"}\e\\'
```

### Implementation

In `screen_osc.cpp`, add handler for OSC number 7770:

1. Parse JSON payload with nlohmann::json
2. If `pane_id` is missing, inject the current pane's ID
3. Call `HookBridge::processHookEvent(json)` — same path as hook scripts

### Error handling

- Malformed JSON: silently ignore (don't crash or log to terminal output)
- Unknown event types: silently ignore (forward compatibility)
- Missing required fields: HookBridge already handles gracefully

## Component 3: Environment Variable Advertising

### PaneEnvironment additions

```cpp
// Add to toEnvVars():
vars.emplace_back("BREADTERMINAL_OSC_CHANNEL", "7770");
vars.emplace_back("BREADTERMINAL_VERSION", TERMCORE_VERSION);
```

This allows any tool to discover the OSC channel at runtime:

```bash
if [ -n "$BREADTERMINAL_OSC_CHANNEL" ]; then
    printf "\e]${BREADTERMINAL_OSC_CHANNEL};{\"event\":\"Notification\",...}\e\\"
fi
```

## Component 4: bread osc emit Command

### CLI interface

```bash
# Emit a state change
bread osc emit state-change --state thinking
bread osc emit state-change --state idle --agent-id a1

# Emit a notification
bread osc emit notify --title "Build" --body "Complete" --urgency normal

# Emit subagent lifecycle
bread osc emit subagent-start --agent-id a1 --agent-type worker --description "Building"
bread osc emit subagent-stop --agent-id a1

# Emit raw JSON
bread osc emit raw '{"event":"StateChange","state":"thinking"}'
```

### Implementation

`osc_emitter.cpp`: constructs JSON payload, outputs `printf '\e]7770;...\e\\'` to stdout. No socket connection needed — this is pure terminal output.

This differs from `bread hook-event` (which sends via IPC). `bread osc emit` writes to stdout, so it works even without the BreadTerminal socket server running.

## Component 5: Universal Hook Installer

### Refactored bread hooks install

```bash
# Install for specific provider
bread hooks install --provider claude_code
bread hooks install --provider codex

# Install for all detected providers
bread hooks install --all

# List available providers and installation status
bread hooks status
```

### How it works

1. Read `ProviderRegistry` to get the target provider's `ProviderHooksConfig`
2. Expand `config_dir` (resolve `~`)
3. For each event in `hooks.events`:
   a. Generate a bash script that maps the tool's env vars to BreadTerminal's JSON format:
      ```bash
      #!/bin/bash
      bread hook-event --json "{\"event\":\"SubagentStart\",\"agent_id\":\"$CLAUDE_AGENT_ID\",...}"
      ```
   b. Write to `<config_dir>/hooks/<hook_name>.sh`
   c. Make executable
4. Update the tool's settings file (`settings_format` determines JSON/YAML/TOML writer):
   - Register each hook script in the tool's hook configuration
5. Mark provider as installed in ProviderRegistry

### Settings file writers

- **JSON**: existing nlohmann::json (already used)
- **YAML**: simple key-value writer (no full YAML library needed — hook configs are flat)
- **TOML**: simple key-value writer (same approach)

For MVP, only JSON is needed (Claude Code, Codex, Gemini CLI all use JSON settings). YAML/TOML support can be added when a tool requires it.

## Component 6: Auto-Detection and Install Notification

### Flow

1. `AgentTracker::detectAgent()` identifies a new tool in a pane
2. `AgentTracker` notifies `ProviderRegistry` of detected agent type
3. `ProviderRegistry` checks:
   - Does this provider have a hooks config? (if not, no notification needed)
   - Are hooks already installed? (if yes, skip)
4. If hooks not installed → add Critical notification:
   ```
   Title: "<display_name> 감지됨"
   Body:  "hook을 설치하면 서브에이전트 추적, 알림 등을 사용할 수 있습니다."
   Source: NotificationSource::System
   Urgency: NotificationUrgency::Critical
   ```
5. Notification includes provider_id in metadata for action routing
6. When user clicks notification (or runs `bread hooks install --provider <id>`):
   - Universal installer runs
   - Notification dismissed
   - Provider marked as installed
7. Installed state persisted in `~/.config/breadterminal/providers_installed.json`

### Suppression

- Each provider only triggers one notification per session
- Once installed, never triggers again (persisted state)
- Config option: `auto_detect_providers = true` (default true, can disable)

## Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| OSC number | 7770 | Unused range, no conflicts with known terminals |
| JSON protocol | Reuse HookBridge format | Zero new parsing code, existing pipeline |
| Provider metadata | Lua tables | Existing Lua infra, user-extensible |
| Hook installer | Generic from Lua metadata | One C++ function serves all providers |
| Settings format | JSON first, YAML/TOML later | All current priority tools use JSON |
| Auto-detect UX | Critical notification | Reuses existing NotificationStore, non-invasive |
| Install persistence | File-based JSON | Simple, no DB needed |

## File Structure

### New files

```
core/defaults/providers.lua              — Provider metadata for 8 built-in tools
core/include/termcore/provider_registry.h — ProviderInfo structs + ProviderRegistry class
core/src/provider_registry.cpp           — Registry implementation + install state persistence
core/src/lua_bindings/lua_provider_module.cpp — terminal.provider() Lua binding
tools/bread/osc_emitter.h               — OSC emit command declarations
tools/bread/osc_emitter.cpp             — printf-based OSC 7770 output
tests/test_provider_registry.cpp         — ProviderRegistry unit tests
tests/test_osc_7770.cpp                 — OSC 7770 parsing integration tests
tests/test_osc_emitter.cpp              — bread osc emit command tests
```

### Modified files

```
core/src/screen_osc.cpp                  — Add OSC 7770 handler
core/include/termcore/pane_environment.h — Add BREADTERMINAL_OSC_CHANNEL env var
core/src/agent.cpp                       — Integrate ProviderRegistry for detection notification
core/CMakeLists.txt                      — Add new source files
tools/bread/hooks_installer.cpp          — Refactor to use ProviderRegistry metadata
tools/bread/arg_parser.cpp               — Add 'osc emit' and 'hooks status' commands
tools/bread/CMakeLists.txt               — Add osc_emitter.cpp
tests/CMakeLists.txt                     — Add new test files
```

## Testing Strategy

- **ProviderRegistry**: register, detect, find, install state — pure unit tests
- **OSC 7770 parsing**: feed OSC sequence to Screen, verify HookBridge receives correct JSON
- **OSC emitter**: verify stdout output format
- **Hook installer**: mock filesystem, verify correct scripts and settings generated per provider
- **Auto-detection flow**: integration test — detect agent → check notification created
