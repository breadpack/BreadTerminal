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

// ---------- Version ----------
const char* termcore_version(void);

#ifdef __cplusplus
}
#endif

#endif // TERMCORE_TERMCORE_H
