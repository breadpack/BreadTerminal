-- BreadTerminal default command palette entries
-- Users can add/remove commands in config.lua

-- Tab/Pane
terminal.command.register("New Tab",       function() terminal.action("new_tab") end,       {category="Tab",  description="Open a new tab"})
terminal.command.register("Close Tab",     function() terminal.action("close_tab") end,     {category="Tab",  description="Close the current tab"})
terminal.command.register("Next Tab",      function() terminal.action("next_tab") end,      {category="Tab",  description="Switch to the next tab"})
terminal.command.register("Previous Tab",  function() terminal.action("prev_tab") end,      {category="Tab",  description="Switch to the previous tab"})
terminal.command.register("Split Right",   function() terminal.action("split_right") end,   {category="Pane", description="Split pane to the right"})
terminal.command.register("Split Down",    function() terminal.action("split_down") end,    {category="Pane", description="Split pane downward"})
terminal.command.register("Close Pane",    function() terminal.action("close_pane") end,    {category="Pane", description="Close the current pane"})

-- Clipboard
terminal.command.register("Copy",               function() terminal.action("copy") end,               {category="Clipboard", description="Copy selection to clipboard"})
terminal.command.register("Paste",              function() terminal.action("paste") end,              {category="Clipboard", description="Paste from clipboard"})
terminal.command.register("Paste from History", function() terminal.action("paste_from_history") end, {category="Clipboard", description="Paste from clipboard history"})
terminal.command.register("Select All",         function() terminal.action("select_all") end,         {category="Clipboard", description="Select all text"})

-- Search
terminal.command.register("Find",          function() terminal.action("search_open") end,  {category="Search", description="Open search bar"})
terminal.command.register("Find Next",     function() terminal.action("search_next") end,  {category="Search", description="Jump to next match"})
terminal.command.register("Find Previous", function() terminal.action("search_prev") end,  {category="Search", description="Jump to previous match"})

-- Scroll
terminal.command.register("Scroll Up",        function() terminal.action("scroll_up") end,        {category="Scroll", description="Scroll up a few lines"})
terminal.command.register("Scroll Down",      function() terminal.action("scroll_down") end,      {category="Scroll", description="Scroll down a few lines"})
terminal.command.register("Scroll Page Up",   function() terminal.action("scroll_page_up") end,   {category="Scroll", description="Scroll up one page"})
terminal.command.register("Scroll Page Down", function() terminal.action("scroll_page_down") end, {category="Scroll", description="Scroll down one page"})
terminal.command.register("Scroll to Top",    function() terminal.action("scroll_to_top") end,    {category="Scroll", description="Scroll to top of history"})
terminal.command.register("Scroll to Bottom", function() terminal.action("scroll_to_bottom") end, {category="Scroll", description="Scroll to bottom"})

-- Window
terminal.command.register("New Window",        function() terminal.action("new_window") end,        {category="Window", description="Open a new window"})
terminal.command.register("Close Window",      function() terminal.action("close_window") end,      {category="Window", description="Close the window"})
terminal.command.register("Toggle Fullscreen", function() terminal.action("toggle_fullscreen") end, {category="Window", description="Toggle fullscreen mode"})

-- Font
terminal.command.register("Increase Font Size", function() terminal.action("font_increase") end, {category="Font", description="Make text larger"})
terminal.command.register("Decrease Font Size", function() terminal.action("font_decrease") end, {category="Font", description="Make text smaller"})
terminal.command.register("Reset Font Size",    function() terminal.action("font_reset") end,    {category="Font", description="Reset to default size"})

-- Terminal
terminal.command.register("Reset Terminal",     function() terminal.action("reset_terminal") end,     {category="Terminal", description="Reset the terminal state"})
terminal.command.register("Clear Scrollback",   function() terminal.action("clear_scrollback") end,   {category="Terminal", description="Clear scrollback buffer"})
terminal.command.register("Reload Config",      function() terminal.action("reload_config") end,      {category="Terminal", description="Reload configuration"})
terminal.command.register("Enter Copy Mode",    function() terminal.action("enter_copy_mode") end,    {category="Terminal", description="Enter vi-style copy mode"})
terminal.command.register("Toggle Broadcast",   function() terminal.action("toggle_broadcast") end,   {category="Pane",     description="Toggle broadcast mode"})

-- Navigation
terminal.command.register("Jump to Previous Prompt", function() terminal.action("jump_prompt_up") end,   {category="Navigation", description="Navigate to previous prompt"})
terminal.command.register("Jump to Next Prompt",     function() terminal.action("jump_prompt_down") end, {category="Navigation", description="Navigate to next prompt"})

-- Settings
terminal.command.register("Toggle Sidebar",  function() terminal.action("toggle_sidebar") end,  {category="Settings", description="Show or hide sidebar"})
terminal.command.register("Open Settings",   function() terminal.action("open_settings") end,   {category="Settings", description="Open settings window"})
terminal.command.register("Open Theme Hub",  function() terminal.action("open_theme_hub") end,  {category="Settings", description="Browse and apply themes"})
terminal.command.register("Open Font Hub",   function() terminal.action("open_font_hub") end,   {category="Settings", description="Browse and apply fonts"})
terminal.command.register("Open Command Palette", function() terminal.action("open_command_palette") end, {category="Settings", description="Open command palette"})
