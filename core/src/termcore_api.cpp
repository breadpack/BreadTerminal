#include "termcore/termcore.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace termcore;

// ---------- Internal structures ----------

struct TermPane {
    TermCore* core = nullptr;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<VtParser> parser;
    std::unique_ptr<Pty> pty;
    std::string line_text_buf; // temp buffer for tc_pane_get_line_text

    TermPane(TermCore* c, int rows, int cols)
        : core(c),
          screen(std::make_unique<Screen>(rows, cols)),
          parser(std::make_unique<VtParser>(*screen)) {}
};

struct TermCore {
    std::vector<TermPane*> panes;
    tc_notify_callback notify_cb = nullptr;
    void* notify_user_data = nullptr;
};

// ---------- Lifecycle ----------

extern "C" {

TermCore* tc_create(void) {
    return new TermCore();
}

void tc_destroy(TermCore* core) {
    if (!core) return;
    // Destroy all owned panes
    for (auto* pane : core->panes) {
        delete pane;
    }
    core->panes.clear();
    delete core;
}

// ---------- Pane management ----------

TermPane* tc_pane_create(TermCore* core, int rows, int cols) {
    if (!core) return nullptr;
    auto* pane = new TermPane(core, rows, cols);
    core->panes.push_back(pane);
    return pane;
}

void tc_pane_destroy(TermPane* pane) {
    if (!pane) return;
    if (pane->core) {
        auto& panes = pane->core->panes;
        panes.erase(std::remove(panes.begin(), panes.end(), pane),
                     panes.end());
    }
    delete pane;
}

void tc_pane_resize(TermPane* pane, int rows, int cols) {
    if (!pane || !pane->screen) return;
    pane->screen->resize(rows, cols);
    if (pane->pty) {
        pane->pty->resize(rows, cols);
    }
}

// ---------- Feed data ----------

void tc_pane_feed(TermPane* pane, const char* data, size_t len) {
    if (!pane || !pane->parser || !data) return;
    pane->parser->feed(data, len);
}

// ---------- Screen query ----------

void tc_pane_get_cell(TermPane* pane, int row, int col, TermCellData* out) {
    if (!pane || !pane->screen || !out) return;
    const auto& cell = pane->screen->cellAt(row, col);
    out->codepoint = static_cast<uint32_t>(cell.codepoint);
    out->fg_color = cell.fg_color;
    out->bg_color = cell.bg_color;
    out->attributes = cell.attributes;
    out->width = cell.width;
}

void tc_pane_get_cursor(TermPane* pane, TermCursorData* out) {
    if (!pane || !pane->screen || !out) return;
    out->row = pane->screen->cursorRow();
    out->col = pane->screen->cursorCol();
    out->visible = pane->screen->cursorVisible() ? 1 : 0;
}

int tc_pane_rows(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return pane->screen->rows();
}

int tc_pane_cols(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return pane->screen->cols();
}

const char* tc_pane_get_line_text(TermPane* pane, int row) {
    if (!pane || !pane->screen) return "";
    pane->line_text_buf = pane->screen->getLineText(row);
    return pane->line_text_buf.c_str();
}

// ---------- PTY I/O ----------

int tc_pane_spawn(TermPane* pane, const char* command) {
    if (!pane || !pane->screen) return -1;
    pane->pty = createPty();
    if (!pane->pty) return -1;

    std::string cmd;
    if (command && command[0] != '\0') {
        cmd = command;
    }
    int rows = pane->screen->rows();
    int cols = pane->screen->cols();
    bool ok = pane->pty->spawn(cmd, {}, "", rows, cols);
    return ok ? 0 : -1;
}

int tc_pane_read_pty(TermPane* pane, char* buf, size_t buf_size) {
    if (!pane || !pane->pty) return -1;
    return pane->pty->read(buf, buf_size);
}

int tc_pane_write_pty(TermPane* pane, const char* data, size_t len) {
    if (!pane || !pane->pty) return -1;
    return pane->pty->write(data, len);
}

int tc_pane_is_alive(TermPane* pane) {
    if (!pane || !pane->pty) return 0;
    return pane->pty->isAlive() ? 1 : 0;
}

// ---------- Notification callback ----------

void tc_set_notify_callback(TermCore* core, tc_notify_callback cb,
                            void* user_data) {
    if (!core) return;
    core->notify_cb = cb;
    core->notify_user_data = user_data;
}

// ---------- Version ----------

const char* termcore_version(void) {
    return "0.1.0";
}

} // extern "C"
