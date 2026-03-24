-- BreadTerminal default configuration
-- Users can override any value in their config.lua

terminal.config({
    font_family = "Menlo",
    font_size = 14.0,
    font_subpixel = "auto",
    font_hinting = "auto",
    font_ligatures = true,

    scrollback_limit = 10000,
    cursor_style = "block",
    cursor_blink = true,
    cursor_blink_interval = 0.5,
    shell = "",

    clipboard_paste_protection = "multiline",
    clipboard_paste_bracketed_safe = true,
    allow_clipboard_write = false,

    clickable_urls = true,
    url_color = 0x89b4fa,

    notify_on_command_finish = true,
    notify_after_seconds = 5.0,

    custom_shader = "none",
    shader_intensity = 1.0,
    background_opacity = 1.0,
    background_blur = 0.5,
    background_blur_mode = "none",
    background_blur_material = "none",

    window_width = 800,
    window_height = 600,
    window_padding = 0,
    minimum_contrast = 1.0,

    sidebar_visible = true,
    sidebar_width = 220,

    quick_terminal_height = 0.4,
    quick_terminal_animation_ms = 150,
    quick_terminal_position = "top",
    quick_terminal_auto_hide = true,

    auto_detect_high_contrast = true,
    respect_reduced_motion = true,

    image_preview = false,
    image_preview_max_height = 10,

    session_autosave = true,
    session_autosave_interval = 30,
})
