-- BreadTerminal default command palette entries
-- Users can add/remove commands in config.lua

local commands = {
    -- Tab/Pane
    { "New Tab",           "new_tab",           "Tab",       "Open a new tab" },
    { "Close Tab",         "close_tab",         "Tab",       "Close the current tab" },
    { "Next Tab",          "next_tab",          "Tab",       "Switch to the next tab" },
    { "Previous Tab",      "prev_tab",          "Tab",       "Switch to the previous tab" },
    { "Split Right",       "split_right",       "Pane",      "Split pane to the right" },
    { "Split Down",        "split_down",        "Pane",      "Split pane downward" },
    { "Close Pane",        "close_pane",        "Pane",      "Close the current pane" },

    -- Clipboard
    { "Copy",              "copy",              "Clipboard", "Copy selection to clipboard" },
    { "Paste",             "paste",             "Clipboard", "Paste from clipboard" },
    { "Paste from History","paste_from_history","Clipboard", "Paste from clipboard history" },
    { "Select All",        "select_all",        "Clipboard", "Select all text" },

    -- Search
    { "Find",              "search_open",       "Search",    "Open search bar" },
    { "Find Next",         "search_next",       "Search",    "Jump to next match" },
    { "Find Previous",     "search_prev",       "Search",    "Jump to previous match" },

    -- Scroll
    { "Scroll Up",         "scroll_up",         "Scroll",    "Scroll up a few lines" },
    { "Scroll Down",       "scroll_down",       "Scroll",    "Scroll down a few lines" },
    { "Scroll Page Up",    "scroll_page_up",    "Scroll",    "Scroll up one page" },
    { "Scroll Page Down",  "scroll_page_down",  "Scroll",    "Scroll down one page" },
    { "Scroll to Top",     "scroll_to_top",     "Scroll",    "Scroll to top of history" },
    { "Scroll to Bottom",  "scroll_to_bottom",  "Scroll",    "Scroll to bottom" },

    -- Window
    { "New Window",        "new_window",        "Window",    "Open a new window" },
    { "Close Window",      "close_window",      "Window",    "Close the window" },
    { "Toggle Fullscreen", "toggle_fullscreen", "Window",    "Toggle fullscreen mode" },

    -- Font
    { "Increase Font Size","font_increase",     "Font",      "Make text larger" },
    { "Decrease Font Size","font_decrease",     "Font",      "Make text smaller" },
    { "Reset Font Size",   "font_reset",        "Font",      "Reset to default size" },

    -- Terminal
    { "Reset Terminal",    "reset_terminal",    "Terminal",  "Reset the terminal state" },
    { "Clear Scrollback",  "clear_scrollback",  "Terminal",  "Clear scrollback buffer" },
    { "Reload Config",     "reload_config",     "Terminal",  "Reload configuration" },
    { "Enter Copy Mode",   "enter_copy_mode",   "Terminal",  "Enter vi-style copy mode" },
    { "Toggle Broadcast",  "toggle_broadcast",  "Pane",      "Toggle broadcast mode" },

    -- Navigation
    { "Jump to Previous Prompt", "jump_prompt_up",   "Navigation", "Navigate to previous prompt" },
    { "Jump to Next Prompt",     "jump_prompt_down", "Navigation", "Navigate to next prompt" },

    -- Settings
    { "Toggle Sidebar",    "toggle_sidebar",    "Settings",  "Show or hide sidebar" },
    { "Open Settings",     "open_settings",     "Settings",  "Open settings window" },
    { "Open Theme Hub",    "open_theme_hub",    "Settings",  "Browse and apply themes" },
    { "Open Font Hub",     "open_font_hub",     "Settings",  "Browse and apply fonts" },
    { "Open Command Palette", "open_command_palette", "Settings", "Open command palette" },
}

for _, cmd in ipairs(commands) do
    local name, action, category, description = cmd[1], cmd[2], cmd[3], cmd[4]
    terminal.command.register(name, function() terminal.action(action) end,
        { category = category, description = description })
end
