#include "hooks_installer.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace bread {

std::vector<ProviderEntry> getBuiltinProviders() {
    return {
        {
            "claude_code", "Claude Code",
            "~/.claude", "settings.json", "json",
            {
                {"SubagentStart", "subagent-start", {{"agent_id", "CLAUDE_AGENT_ID"}, {"agent_type", "CLAUDE_AGENT_TYPE"}, {"description", "CLAUDE_AGENT_DESCRIPTION"}}},
                {"SubagentStop", "subagent-stop", {{"agent_id", "CLAUDE_AGENT_ID"}}},
                {"Notification", "notification", {{"body", "CLAUDE_NOTIFICATION_MESSAGE"}}},
                {"PostTool", "post-tool", {{"tool_name", "CLAUDE_TOOL_NAME"}}},
            }
        },
        {
            "codex", "Codex",
            "~/.codex", "config.json", "json",
            {}
        },
        {
            "gemini_cli", "Gemini CLI",
            "~/.gemini", "settings.json", "json",
            {}
        },
        {"aider", "Aider", "", "", "", {}},
        {"opencode", "OpenCode", "", "", "", {}},
        {"goose", "Goose", "", "", "", {}},
        {"amp", "Amp", "", "", "", {}},
        {"cline", "Cline", "", "", "", {}},
    };
}

static std::filesystem::path expandHome(const std::string& path) {
    if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
        const char* home = std::getenv("HOME");
        if (!home) home = std::getenv("USERPROFILE");
        if (home) return std::filesystem::path(home) / path.substr(2);
    }
    return path;
}

static bool writeScript(const std::filesystem::path& path, const std::string& content) {
    std::ofstream ofs(path);
    if (!ofs) {
        std::cerr << "Error: Cannot write to " << path << "\n";
        return false;
    }
    ofs << content;
    ofs.close();

    std::filesystem::permissions(path,
        std::filesystem::perms::owner_exec |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
    return true;
}

static std::string generateHookScript(const ProviderEntry::HookEvent& event) {
    std::string script = "#!/bin/bash\n";
    script += "# BreadTerminal hook — event: " + event.bread_event + "\n";

    // Build JSON fields from env_map
    std::string json_fields = "\"event\":\"" + event.bread_event + "\"";
    for (const auto& [field, env_var] : event.env_map) {
        json_fields += ",\"" + field + "\":\"'\"$" + env_var + "\"'\"";
    }
    json_fields += ",\"pane_id\":\"'\"$BREADTERMINAL_PANE_ID\"'\"";

    script += "bread hook-event --json '{" + json_fields + "}'\n";
    return script;
}

static void updateSettings(const std::filesystem::path& settings_path,
                           const std::filesystem::path& hooks_dir,
                           const std::vector<ProviderEntry::HookEvent>& events) {
    nlohmann::json settings;

    if (std::filesystem::exists(settings_path)) {
        std::ifstream ifs(settings_path);
        if (ifs) {
            try {
                settings = nlohmann::json::parse(ifs);
            } catch (...) {
                std::cerr << "Warning: Could not parse " << settings_path
                          << ", creating new settings.\n";
                settings = nlohmann::json::object();
            }
        }
    }

    if (!settings.contains("hooks")) {
        settings["hooks"] = nlohmann::json::object();
    }

    for (const auto& event : events) {
        auto script_path = (hooks_dir / (event.hook_name + ".sh")).string();
        settings["hooks"][event.hook_name] = nlohmann::json::array({
            {{"command", script_path}, {"type", "command"}}
        });
    }

    std::ofstream ofs(settings_path);
    if (ofs) {
        ofs << settings.dump(2) << "\n";
        std::cout << "Updated " << settings_path << "\n";
    } else {
        std::cerr << "Warning: Could not write to " << settings_path << "\n";
    }
}

int installHooksForProvider(const std::string& provider_id) {
    auto providers = getBuiltinProviders();
    const ProviderEntry* provider = nullptr;
    for (const auto& p : providers) {
        if (p.id == provider_id) { provider = &p; break; }
    }

    if (!provider) {
        std::cerr << "Error: Unknown provider '" << provider_id << "'\n";
        std::cerr << "Available providers: ";
        for (const auto& p : providers) {
            if (p.has_hooks()) std::cerr << p.id << " ";
        }
        std::cerr << "\n";
        return 1;
    }

    if (!provider->has_hooks()) {
        std::cerr << "Error: " << provider->display_name
                  << " has no hook system (uses OSC channel only).\n";
        return 1;
    }

    if (provider->events.empty()) {
        std::cout << provider->display_name
                  << " has no hook events defined yet.\n";
        return 0;
    }

    auto config_dir = expandHome(provider->config_dir);
    auto hooks_dir = config_dir / "hooks";

    std::error_code ec;
    std::filesystem::create_directories(hooks_dir, ec);
    if (ec) {
        std::cerr << "Error: Could not create " << hooks_dir << ": " << ec.message() << "\n";
        return 1;
    }

    bool ok = true;
    for (const auto& event : provider->events) {
        auto script = generateHookScript(event);
        ok &= writeScript(hooks_dir / (event.hook_name + ".sh"), script);
    }

    if (!ok) return 1;

    std::cout << "Installed hook scripts to " << hooks_dir << "\n";

    auto settings_path = config_dir / provider->settings_file;
    updateSettings(settings_path, hooks_dir, provider->events);

    std::cout << "\nDone! " << provider->display_name << " hooks installed.\n";
    for (const auto& event : provider->events) {
        std::cout << "  " << event.hook_name << ".sh — "
                  << event.bread_event << " event\n";
    }
    return 0;
}

int installAllHooks() {
    auto providers = getBuiltinProviders();
    int errors = 0;
    for (const auto& p : providers) {
        if (p.has_hooks() && !p.events.empty()) {
            std::cout << "\n--- Installing hooks for " << p.display_name << " ---\n";
            errors += installHooksForProvider(p.id);
        }
    }
    return errors > 0 ? 1 : 0;
}

int showHooksStatus() {
    auto providers = getBuiltinProviders();
    if (providers.empty()) {
        std::cout << "No providers registered.\n";
        return 0;
    }

    std::cout << "AI CLI Provider Status:\n\n";
    for (const auto& p : providers) {
        std::cout << "  " << p.display_name;
        if (!p.has_hooks()) {
            std::cout << " -- OSC channel only (no hook system)\n";
        } else if (p.events.empty()) {
            std::cout << " -- hook system available, no events defined yet\n";
        } else {
            // Check if hooks directory exists
            auto config_dir = expandHome(p.config_dir);
            auto hooks_dir = config_dir / "hooks";
            bool installed = std::filesystem::exists(hooks_dir / (p.events[0].hook_name + ".sh"));
            if (installed) {
                std::cout << " -- hooks installed\n";
            } else {
                std::cout << " -- hooks not installed"
                          << " (run: bread hooks install --provider " << p.id << ")\n";
            }
        }
    }
    return 0;
}

int installHooks() {
    // Legacy: install Claude Code hooks
    return installHooksForProvider("claude_code");
}

}  // namespace bread
