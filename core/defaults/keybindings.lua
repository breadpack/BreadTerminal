-- BreadTerminal default keybindings
-- Users can override in config.lua with terminal.keymap()

local mod = terminal.platform == "macos" and "super" or "ctrl"

-- Tab
terminal.keymap(mod .. "+t", "new_tab")
terminal.keymap(mod .. "+w", "close_tab")
terminal.keymap(mod .. "+shift+]", "next_tab")
terminal.keymap(mod .. "+shift+[", "prev_tab")

-- Pane
terminal.keymap(mod .. "+d", "split_right")
terminal.keymap(mod .. "+shift+d", "split_down")

-- Tab switching 1-9
for i = 1, 9 do
    terminal.keymap(mod .. "+" .. i, "switch_tab_" .. i)
end

-- Clipboard
terminal.keymap(mod .. "+c", "copy")
terminal.keymap(mod .. "+v", "paste")
terminal.keymap(mod .. "+shift+v", "paste_from_history")
terminal.keymap(mod .. "+a", "select_all")

-- Search
terminal.keymap(mod .. "+f", "search_open")
terminal.keymap(mod .. "+g", "search_next")
terminal.keymap(mod .. "+shift+g", "search_prev")

-- Font
terminal.keymap(mod .. "+=", "font_increase")
terminal.keymap(mod .. "+-", "font_decrease")
terminal.keymap(mod .. "+0", "font_reset")

-- Scroll
terminal.keymap("shift+pageup", "scroll_page_up")
terminal.keymap("shift+pagedown", "scroll_page_down")
terminal.keymap("shift+home", "scroll_to_top")
terminal.keymap("shift+end", "scroll_to_bottom")

-- Window
terminal.keymap(mod .. "+n", "new_window")
terminal.keymap(mod .. "+enter", "toggle_fullscreen")

-- Misc
terminal.keymap(mod .. "+k", "clear_scrollback")
terminal.keymap(mod .. "+up", "jump_prompt_up")
terminal.keymap(mod .. "+down", "jump_prompt_down")
terminal.keymap(mod .. "+shift+,", "reload_config")
terminal.keymap(mod .. "+shift+x", "enter_copy_mode")
terminal.keymap(mod .. "+shift+b", "toggle_sidebar")

-- Settings/UI
terminal.keymap(mod .. "+,", "open_settings")
terminal.keymap(mod .. "+shift+t", "open_theme_hub")
terminal.keymap(mod .. "+shift+p", "open_command_palette")
terminal.keymap(mod .. "+shift+f", "open_font_hub")

-- Profiles
terminal.keymap(mod .. "+shift+n", "show_profile_dropdown")
for i = 1, 9 do
    terminal.keymap(mod .. "+shift+" .. i, "new_tab_profile_" .. i)
end
