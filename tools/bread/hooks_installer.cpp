#include "hooks_installer.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace bread {

static const char* kNotificationHookScript = R"(#!/bin/bash
# BreadTerminal notification hook for Claude Code
# Install: bread hooks install
bread notify send --title "Claude Code" --body "$CLAUDE_NOTIFICATION_MESSAGE"
)";

static const char* kPostToolHookScript = R"(#!/bin/bash
# BreadTerminal post-tool hook for Claude Code
# Install: bread hooks install
bread notify send --title "Tool: $CLAUDE_TOOL_NAME" --body "Completed" --urgency low
)";

static bool writeScript(const std::filesystem::path& path, const char* content) {
    std::ofstream ofs(path);
    if (!ofs) {
        std::cerr << "Error: Cannot write to " << path << "\n";
        return false;
    }
    ofs << content;
    ofs.close();

    // Make executable
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_exec |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
    return true;
}

static void updateSettings(const std::filesystem::path& settings_path,
                           const std::filesystem::path& hooks_dir) {
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

    // Register hooks in settings
    auto notification_path = (hooks_dir / "notification.sh").string();
    auto post_tool_path = (hooks_dir / "post-tool.sh").string();

    if (!settings.contains("hooks")) {
        settings["hooks"] = nlohmann::json::object();
    }

    // Notification hook
    settings["hooks"]["notification"] = nlohmann::json::array({
        {{"command", notification_path}, {"type", "command"}}
    });

    // Post-tool hook
    settings["hooks"]["post-tool"] = nlohmann::json::array({
        {{"command", post_tool_path}, {"type", "command"}}
    });

    std::ofstream ofs(settings_path);
    if (ofs) {
        ofs << settings.dump(2) << "\n";
        std::cout << "Updated " << settings_path << "\n";
    } else {
        std::cerr << "Warning: Could not write to " << settings_path << "\n";
    }
}

int installHooks() {
    const char* home = std::getenv("HOME");
    if (!home) {
        std::cerr << "Error: HOME environment variable not set.\n";
        return 1;
    }

    std::filesystem::path claude_dir = std::filesystem::path(home) / ".claude";
    std::filesystem::path hooks_dir = claude_dir / "hooks";

    // Create hooks directory
    std::error_code ec;
    std::filesystem::create_directories(hooks_dir, ec);
    if (ec) {
        std::cerr << "Error: Could not create " << hooks_dir << ": " << ec.message() << "\n";
        return 1;
    }

    // Write hook scripts
    bool ok = true;
    ok &= writeScript(hooks_dir / "notification.sh", kNotificationHookScript);
    ok &= writeScript(hooks_dir / "post-tool.sh", kPostToolHookScript);

    if (!ok) {
        return 1;
    }

    std::cout << "Installed hook scripts to " << hooks_dir << "\n";

    // Update settings.json if it exists (or create minimal one)
    auto settings_path = claude_dir / "settings.json";
    updateSettings(settings_path, hooks_dir);

    std::cout << "\nDone! Claude Code hooks installed.\n"
              << "  notification.sh — triggers BreadTerminal notification on Claude messages\n"
              << "  post-tool.sh   — notifies when a tool completes\n"
              << "\nTo verify, run: cat " << settings_path << "\n";

    return 0;
}

}  // namespace bread
