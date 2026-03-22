// Config → Lua serializer. No Lua runtime dependency — always compiled.

#include "termcore/lua_config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace termcore {
namespace {

std::string hexColor(uint32_t c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%06x", c & 0xFFFFFF);
    return buf;
}

std::string escLua(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    out += '"';
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    out += '"';
    return out;
}

} // anonymous namespace

std::string serializeConfigLua(const Config& config) {
    std::ostringstream o;
    o << "-- BreadTerminal Configuration (auto-generated)\n";
    o << "-- Edit freely. Changes are applied on save.\n\n";

    // Platform helper
    o << "local mod = terminal.platform == \"macos\" and \"cmd\" or \"ctrl\"\n\n";

    // Font
    o << "terminal.config({\n";
    o << "    font_family = " << escLua(config.font_family) << ",\n";
    o << "    font_size = " << config.font_size << ",\n";
    if (!config.font_features.empty()) {
        o << "    font_features = { ";
        for (size_t i = 0; i < config.font_features.size(); ++i) {
            if (i > 0) o << ", ";
            o << escLua(config.font_features[i]);
        }
        o << " },\n";
    }
    o << "})\n\n";

    // Theme & Colors
    o << "terminal.config({\n";
    if (!config.theme.empty()) {
        o << "    theme = " << escLua(config.theme) << ",\n";
    }
    o << "    background = " << hexColor(config.background) << ",\n";
    o << "    foreground = " << hexColor(config.foreground) << ",\n";
    o << "    cursor_color = " << hexColor(config.cursor_color) << ",\n";
    o << "    selection_background = " << hexColor(config.selection_background) << ",\n";
    o << "    selection_foreground = " << hexColor(config.selection_foreground) << ",\n";
    o << "    palette = {\n";
    for (int i = 0; i < 16; ++i) {
        o << "        " << hexColor(config.palette[i]) << ",";
        if (i == 7) o << "\n";
        else if (i == 15) o << "\n";
        else o << " ";
    }
    o << "    },\n";
    o << "})\n\n";

    // Appearance
    o << "terminal.config({\n";
    o << "    background_opacity = " << config.background_opacity << ",\n";
    o << "    background_blur = " << config.background_blur << ",\n";
    if (config.window_padding > 0)
        o << "    window_padding = " << config.window_padding << ",\n";
    if (config.minimum_contrast > 1.0f)
        o << "    minimum_contrast = " << config.minimum_contrast << ",\n";
    o << "})\n\n";

    // Terminal behavior
    o << "terminal.config({\n";
    o << "    scrollback_limit = " << config.scrollback_limit << ",\n";
    o << "    cursor_style = " << escLua(config.cursor_style) << ",\n";
    o << "    cursor_blink = " << (config.cursor_blink ? "true" : "false") << ",\n";
    o << "    cursor_blink_interval = " << config.cursor_blink_interval << ",\n";
    if (!config.shell.empty())
        o << "    shell = " << escLua(config.shell) << ",\n";
    o << "})\n\n";

    // Clipboard
    o << "terminal.config({\n";
    o << "    clipboard_paste_protection = " << escLua(config.clipboard_paste_protection) << ",\n";
    o << "    clipboard_paste_bracketed_safe = " << (config.clipboard_paste_bracketed_safe ? "true" : "false") << ",\n";
    o << "    allow_clipboard_write = " << (config.allow_clipboard_write ? "true" : "false") << ",\n";
    o << "})\n\n";

    // Clickable URLs
    o << "terminal.config({\n";
    o << "    clickable_urls = " << (config.clickable_urls ? "true" : "false") << ",\n";
    o << "    url_color = 0x" << std::hex << config.url_color << std::dec << ",\n";
    o << "})\n\n";

    // Notifications
    o << "terminal.config({\n";
    o << "    notify_on_command_finish = " << (config.notify_on_command_finish ? "true" : "false") << ",\n";
    o << "    notify_after_seconds = " << config.notify_after_seconds << ",\n";
    o << "})\n\n";

    // Window
    if (config.window_width > 0 || config.window_height > 0) {
        o << "terminal.config({\n";
        if (config.window_width > 0)
            o << "    window_width = " << config.window_width << ",\n";
        if (config.window_height > 0)
            o << "    window_height = " << config.window_height << ",\n";
        o << "})\n\n";
    }

    // Quick terminal
    if (!config.quick_terminal_hotkey.empty()) {
        o << "terminal.config({ quick_terminal_hotkey = "
          << escLua(config.quick_terminal_hotkey) << " })\n\n";
    }

    // Sidebar
    o << "terminal.config({\n";
    o << "    sidebar_visible = " << (config.sidebar_visible ? "true" : "false") << ",\n";
    o << "    sidebar_width = " << config.sidebar_width << ",\n";
    o << "})\n\n";

    // Keybindings
    if (!config.keybindings.empty()) {
        o << "-- Keybindings\n";
        for (const auto& kb : config.keybindings) {
            o << "terminal.keymap(" << escLua(kb.trigger) << ", "
              << escLua(kb.action) << ")\n";
        }
        o << "\n";
    }

    return o.str();
}

bool writeConfigLua(const std::string& path, const Config& config) {
    namespace fs = std::filesystem;

    // Create parent directories
    auto parent = fs::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
    }

    // Atomic write: write to .tmp, then rename
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) return false;
        f << serializeConfigLua(config);
        if (!f) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string luaConfigWritePath() {
    std::string base = defaultConfigPath();
    if (base.empty()) return "";

    namespace fs = std::filesystem;
    fs::path dir = fs::path(base).parent_path();

    // Create directory if needed
    std::error_code ec;
    fs::create_directories(dir, ec);

    return (dir / "config.lua").string();
}

} // namespace termcore
