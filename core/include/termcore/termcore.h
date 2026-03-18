#ifndef TERMCORE_TERMCORE_H
#define TERMCORE_TERMCORE_H

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Opaque handle ----------
// typedef struct TermSession TermSession;

// ---------- Lifecycle ----------
// TermSession* term_session_create(int rows, int cols);
// void         term_session_destroy(TermSession* session);

// ---------- I/O ----------
// void term_session_feed(TermSession* session, const char* data, int len);
// const char* term_session_read(TermSession* session, int* out_len);

// ---------- Screen query ----------
// const char* term_screen_get_text(TermSession* session, int row);
// int term_screen_rows(TermSession* session);
// int term_screen_cols(TermSession* session);

// ---------- Notification callback ----------
// typedef void (*TermNotifyCallback)(int event_type, const char* payload, void* user_data);
// void term_session_set_notify(TermSession* session, TermNotifyCallback cb, void* user_data);

/// Returns the library version string.
const char* termcore_version(void);

#ifdef __cplusplus
}
#endif

#endif // TERMCORE_TERMCORE_H
