#include "termcore/hooks_installer.h"
#include "termcore/provider_registry.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace termcore {

std::vector<HookEntry> getBuiltinHookEntries() {
    return {
        {
            "claude_code", "Claude Code",
            "~/.claude", "settings.json", "json",
            {
                {"SubagentStart", "SubagentStart", {{"agent_id", "CLAUDE_AGENT_ID"}, {"agent_type", "CLAUDE_AGENT_TYPE"}, {"description", "CLAUDE_AGENT_DESCRIPTION"}}},
                {"SubagentStop", "SubagentStop", {{"agent_id", "CLAUDE_AGENT_ID"}}},
                {"Notification", "Notification", {{"body", "CLAUDE_NOTIFICATION_MESSAGE"}}},
                {"PostToolUse", "PostToolUse", {{"tool_name", "CLAUDE_TOOL_NAME"}}},
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

static std::string generateHookScript(const HookEntry::HookEvent& event) {
    std::string script = "#!/bin/bash\n";
    script += "# BreadTerminal hook — event: " + event.bread_event + "\n";

    std::string json_fields = "\"event\":\"" + event.bread_event + "\"";
    for (const auto& [field, env_var] : event.env_map) {
        json_fields += ",\"" + field + "\":\"'\"$" + env_var + "\"'\"";
    }
    json_fields += ",\"pane_id\":\"'\"$BREADTERMINAL_PANE_ID\"'\"";

    script += "bread hook-event --json '{" + json_fields + "}'\n";
    return script;
}

static bool writeScript(const std::filesystem::path& path, const std::string& content) {
    std::ofstream ofs(path);
    if (!ofs) return false;
    ofs << content;
    ofs.close();

#ifndef _WIN32
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_exec |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
#endif
    return true;
}

static void updateSettings(const std::filesystem::path& settings_path,
                           const std::filesystem::path& hooks_dir,
                           const std::vector<HookEntry::HookEvent>& events) {
    nlohmann::json settings;

    if (std::filesystem::exists(settings_path)) {
        std::ifstream ifs(settings_path);
        if (ifs) {
            try {
                settings = nlohmann::json::parse(ifs);
            } catch (...) {
                settings = nlohmann::json::object();
            }
        }
    }

    if (!settings.contains("hooks")) {
        settings["hooks"] = nlohmann::json::object();
    }

    // Normalize path for comparison (backslash → forward slash)
    auto normalize = [](std::string s) {
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };

    for (const auto& event : events) {
        auto script_path = (hooks_dir / (event.hook_name + ".sh")).string();
        auto norm_script = normalize(script_path);

        // New format: {"matcher": "", "hooks": [{"type": "command", "command": "..."}]}
        nlohmann::json hook_cmd = {{"type", "command"}, {"command", script_path}};
        nlohmann::json matcher_entry = {{"matcher", ""}, {"hooks", nlohmann::json::array({hook_cmd})}};

        if (settings["hooks"].contains(event.hook_name)) {
            auto& existing = settings["hooks"][event.hook_name];
            if (existing.is_array()) {
                // Check if BreadTerminal hook already registered
                bool already_registered = false;
                for (const auto& entry : existing) {
                    if (!entry.contains("hooks") || !entry["hooks"].is_array()) continue;
                    for (const auto& h : entry["hooks"]) {
                        if (h.contains("command") &&
                            normalize(h["command"].get<std::string>()) == norm_script) {
                            already_registered = true;
                            break;
                        }
                    }
                    if (already_registered) break;
                }
                if (already_registered) continue;

                // Append to existing matchers (preserve user's hooks)
                existing.push_back(matcher_entry);
            }
        } else {
            settings["hooks"][event.hook_name] = nlohmann::json::array({matcher_entry});
        }
    }

    std::ofstream ofs(settings_path);
    if (ofs) {
        ofs << settings.dump(2) << "\n";
    }
}

static const HookEntry* findEntry(const std::string& provider_id) {
    static auto entries = getBuiltinHookEntries();
    for (const auto& e : entries) {
        if (e.id == provider_id) return &e;
    }
    return nullptr;
}

bool isHooksInstalled(const std::string& provider_id) {
    const auto* entry = findEntry(provider_id);
    if (!entry || !entry->has_hooks() || entry->events.empty()) return true;

    auto config_dir = expandHome(entry->config_dir);

    // Check settings.json for BreadTerminal hook entries
    auto settings_path = config_dir / entry->settings_file;
    if (!std::filesystem::exists(settings_path)) return false;

    try {
        std::ifstream ifs(settings_path);
        if (!ifs) return false;
        auto settings = nlohmann::json::parse(ifs);
        if (!settings.contains("hooks") || !settings["hooks"].is_object()) return false;

        auto normalize = [](std::string s) {
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        auto hooks_dir = config_dir / "hooks";

        // Check if all BreadTerminal hooks are registered (new matcher format)
        for (const auto& event : entry->events) {
            if (!settings["hooks"].contains(event.hook_name)) return false;
            auto& arr = settings["hooks"][event.hook_name];
            if (!arr.is_array()) return false;

            auto expected = normalize((hooks_dir / (event.hook_name + ".sh")).string());
            bool found = false;
            for (const auto& matcher_entry : arr) {
                if (!matcher_entry.contains("hooks") || !matcher_entry["hooks"].is_array()) continue;
                for (const auto& h : matcher_entry["hooks"]) {
                    if (h.contains("command") &&
                        normalize(h["command"].get<std::string>()) == expected) {
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

int installHooksForProvider(const std::string& provider_id) {
    const auto* entry = findEntry(provider_id);
    if (!entry || !entry->has_hooks() || entry->events.empty()) return 1;

    auto config_dir = expandHome(entry->config_dir);
    auto hooks_dir = config_dir / "hooks";

    std::error_code ec;
    std::filesystem::create_directories(hooks_dir, ec);
    if (ec) return 1;

    bool ok = true;
    for (const auto& event : entry->events) {
        auto script = generateHookScript(event);
        ok &= writeScript(hooks_dir / (event.hook_name + ".sh"), script);
    }
    if (!ok) return 1;

    auto settings_path = config_dir / entry->settings_file;
    updateSettings(settings_path, hooks_dir, entry->events);
    return 0;
}

// --- Overloads using ProviderHooksConfig from ProviderRegistry ---

static std::vector<HookEntry::HookEvent> convertEvents(const ProviderHooksConfig& hooks) {
    std::vector<HookEntry::HookEvent> events;
    for (const auto& pe : hooks.events) {
        HookEntry::HookEvent he;
        he.bread_event = pe.bread_event;
        he.hook_name = pe.hook_name;
        for (const auto& m : pe.env_map) {
            he.env_map.emplace_back(m.bread_field, m.tool_env_var);
        }
        events.push_back(std::move(he));
    }
    return events;
}

bool isHooksInstalledFromConfig(const ProviderHooksConfig& hooks) {
    if (hooks.empty() || hooks.events.empty()) return true;

    auto config_dir = expandHome(hooks.config_dir);
    auto settings_path = config_dir / hooks.settings_file;
    if (!std::filesystem::exists(settings_path)) return false;

    try {
        std::ifstream ifs(settings_path);
        if (!ifs) return false;
        auto settings = nlohmann::json::parse(ifs);
        if (!settings.contains("hooks") || !settings["hooks"].is_object()) return false;

        auto normalize = [](std::string s) {
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        auto hooks_dir = config_dir / "hooks";
        auto events = convertEvents(hooks);

        for (const auto& event : events) {
            if (!settings["hooks"].contains(event.hook_name)) return false;
            auto& arr = settings["hooks"][event.hook_name];
            if (!arr.is_array()) return false;

            auto expected = normalize((hooks_dir / (event.hook_name + ".sh")).string());
            bool found = false;
            for (const auto& matcher_entry : arr) {
                if (!matcher_entry.contains("hooks") || !matcher_entry["hooks"].is_array()) continue;
                for (const auto& h : matcher_entry["hooks"]) {
                    if (h.contains("command") &&
                        normalize(h["command"].get<std::string>()) == expected) {
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

int installHooksFromConfig(const std::string& /*provider_id*/, const ProviderHooksConfig& hooks) {
    if (hooks.empty() || hooks.events.empty()) return 1;

    auto config_dir = expandHome(hooks.config_dir);
    auto hooks_dir = config_dir / "hooks";

    std::error_code ec;
    std::filesystem::create_directories(hooks_dir, ec);
    if (ec) return 1;

    auto events = convertEvents(hooks);

    bool ok = true;
    for (const auto& event : events) {
        auto script = generateHookScript(event);
        ok &= writeScript(hooks_dir / (event.hook_name + ".sh"), script);
    }
    if (!ok) return 1;

    auto settings_path = config_dir / hooks.settings_file;
    updateSettings(settings_path, hooks_dir, events);
    return 0;
}

}  // namespace termcore
