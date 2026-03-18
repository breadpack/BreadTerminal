#ifndef TERMCORE_TERMCORE_H
#define TERMCORE_TERMCORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Opaque handles ----------
typedef struct TermCore TermCore;
typedef struct TermPane TermPane;

// ---------- Cell data for rendering ----------
typedef struct {
    uint32_t codepoint;
    uint32_t fg_color;
    uint32_t bg_color;
    uint16_t attributes;
    uint8_t width;
} TermCellData;

typedef struct {
    int row;
    int col;
    int visible;
} TermCursorData;

// ---------- Lifecycle ----------
TermCore* tc_create(void);
void tc_destroy(TermCore* core);

// ---------- Pane management ----------
TermPane* tc_pane_create(TermCore* core, int rows, int cols);
void tc_pane_destroy(TermPane* pane);
void tc_pane_resize(TermPane* pane, int rows, int cols);

// ---------- Feed data from PTY output into the pane (parser -> screen) ----------
void tc_pane_feed(TermPane* pane, const char* data, size_t len);

// ---------- Screen query ----------
void tc_pane_get_cell(TermPane* pane, int row, int col, TermCellData* out);
void tc_pane_get_cursor(TermPane* pane, TermCursorData* out);
int tc_pane_rows(TermPane* pane);
int tc_pane_cols(TermPane* pane);
/// Returns text of a line. Pointer valid until next call on same pane.
const char* tc_pane_get_line_text(TermPane* pane, int row);

// ---------- Extended screen query ----------

/// Get terminal title (set by OSC 0/2). Pointer valid until next call on same pane.
const char* tc_pane_get_title(TermPane* pane);

/// Get working directory (set by OSC 7). Pointer valid until next call on same pane.
const char* tc_pane_get_working_dir(TermPane* pane);

/// Get cursor style: 0=Block, 1=Underline, 2=Bar
int tc_pane_get_cursor_style(TermPane* pane);

/// Check if cursor is blinking
int tc_pane_get_cursor_blink(TermPane* pane);

/// Get scrollback line count
int tc_pane_scrollback_size(TermPane* pane);

/// Get scrollback line text. line=0 is most recent (just above screen).
/// Pointer valid until next call on same pane.
const char* tc_pane_get_scrollback_line(TermPane* pane, int line);

/// Check if alternate screen is active
int tc_pane_alt_screen_active(TermPane* pane);

/// Check if bracketed paste mode is active
int tc_pane_bracketed_paste(TermPane* pane);

/// Check if application cursor keys mode is active
int tc_pane_app_cursor_keys(TermPane* pane);

/// Check if any mouse reporting mode is active
int tc_pane_mouse_mode_active(TermPane* pane);

// ---------- PTY I/O ----------
int tc_pane_spawn(TermPane* pane, const char* command);
int tc_pane_read_pty(TermPane* pane, char* buf, size_t buf_size);
int tc_pane_write_pty(TermPane* pane, const char* data, size_t len);
int tc_pane_is_alive(TermPane* pane);

// ---------- Notification callback ----------
typedef void (*tc_notify_callback)(TermPane* pane, int type,
                                   const char* msg, void* user_data);
void tc_set_notify_callback(TermCore* core, tc_notify_callback cb,
                            void* user_data);

// ---------- Mouse event encoding ----------
/// Encode a mouse event into an escape sequence for the PTY.
/// Returns length written to buf, or 0 if mouse reporting is inactive.
/// type: 0=Press, 1=Release, 2=Move, 3=ScrollUp, 4=ScrollDown
/// button: 0=Left, 1=Middle, 2=Right, 3=Release, 4=ScrollUp, 5=ScrollDown
/// mods: bit0=shift, bit1=alt, bit2=ctrl
int tc_pane_encode_mouse(TermPane* pane, int type, int button,
                          int col, int row, int mods,
                          char* buf, size_t buf_size);

// ---------- Mux operations ----------
int tc_workspace_create(TermCore* core, const char* name);
int tc_workspace_active(TermCore* core);
void tc_workspace_set_active(TermCore* core, int workspace_id);
int tc_workspace_count(TermCore* core);
int tc_tab_create(TermCore* core, int workspace_id);
int tc_tab_active(TermCore* core, int workspace_id);
int tc_split_pane(TermCore* core, int workspace_id, int tab_id,
                   int pane_id, int direction);
void tc_close_pane_in_tab(TermCore* core, int workspace_id, int tab_id, int pane_id);
int tc_pane_active_in_tab(TermCore* core, int workspace_id, int tab_id);

// ---------- Agent tracking ----------
int tc_agent_detect(TermCore* core, const char* process_name);
void tc_agent_report_state(TermCore* core, int pane_id, int agent_type, int state, const char* message);
void tc_agent_report_start(TermCore* core, int pane_id, int agent_type, int pid);
void tc_agent_report_exit(TermCore* core, int pane_id);
int tc_agent_get_state(TermCore* core, int pane_id);
int tc_agent_any_needs_input(TermCore* core);
void tc_agent_sweep_stale(TermCore* core);

// ---------- Lua plugin system ----------
int tc_lua_load_plugin(TermCore* core, const char* plugin_path);
int tc_lua_load_string(TermCore* core, const char* code);
void tc_lua_fire_event(TermCore* core, int event_type, const char* data);

// ---------- Notifications ----------
int tc_notification_count(TermCore* core);
int tc_notification_unread_count(TermCore* core);
void tc_notification_mark_all_read(TermCore* core);
void tc_notification_clear(TermCore* core);

// ---------- WebView ----------
typedef struct TermWebView TermWebView;
TermWebView* tc_webview_create(TermCore* core);
void tc_webview_destroy(TermWebView* wv);
void tc_webview_navigate(TermWebView* wv, const char* url);
const char* tc_webview_get_url(TermWebView* wv);
const char* tc_webview_get_title(TermWebView* wv);
void tc_webview_go_back(TermWebView* wv);
void tc_webview_go_forward(TermWebView* wv);
void tc_webview_reload(TermWebView* wv);
int tc_webview_can_go_back(TermWebView* wv);
int tc_webview_can_go_forward(TermWebView* wv);

// ---------- Version ----------
const char* termcore_version(void);

#ifdef __cplusplus
}
#endif

#endif // TERMCORE_TERMCORE_H
