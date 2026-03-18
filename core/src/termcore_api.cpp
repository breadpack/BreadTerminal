#include "termcore/termcore.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/mouse.h"
#include "termcore/mux.h"
#include "termcore/agent.h"
#include "termcore/lua_engine.h"
#include "termcore/notification.h"

#include <algorithm>
#include <cstring>
#include <memory>
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
    std::string title_buf;
    std::string cwd_buf;
    std::string scrollback_buf;

    TermPane(TermCore* c, int rows, int cols)
        : core(c),
          screen(std::make_unique<Screen>(rows, cols)),
          parser(std::make_unique<VtParser>(*screen)) {}
};

struct TermCore {
    std::vector<TermPane*> panes;
    tc_notify_callback notify_cb = nullptr;
    void* notify_user_data = nullptr;

    // Owned subsystems
    termcore::Mux mux;
    termcore::AgentTracker agent_tracker;
    termcore::LuaEngine lua_engine;
    termcore::NotificationStore notifications;
};

// ---------- Lifecycle ----------

extern "C" {

TermCore* tc_create(void) {
    auto* core = new TermCore();

    // Wire Mux pane creation/destruction to real TermPane lifecycle
    core->mux.setPaneCallbacks(
        [core](int rows, int cols) -> termcore::PaneId {
            TermPane* pane = tc_pane_create(core, rows, cols);
            if (!pane) return termcore::kInvalidPane;
            // PaneId is index+1 (0 is kInvalidPane)
            return static_cast<termcore::PaneId>(core->panes.size());
        },
        [core](termcore::PaneId id) {
            if (id == termcore::kInvalidPane) return;
            size_t idx = static_cast<size_t>(id) - 1;
            if (idx < core->panes.size() && core->panes[idx]) {
                tc_pane_destroy(core->panes[idx]);
            }
        }
    );

    return core;
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

    // Wire Screen notification callback -> NotificationStore -> C API callback
    auto* pane_ptr = pane;
    auto* core_ptr = core;
    pane->screen->setNotificationCallback(
        [core_ptr, pane_ptr](const termcore::TermNotification& notif) {
            // Add to notification store
            core_ptr->notifications.add(
                0, termcore::NotificationSource::OSC9,
                termcore::NotificationUrgency::Normal,
                notif.title, notif.body);
            // Fire C API callback
            if (core_ptr->notify_cb) {
                std::string msg = notif.title + ": " + notif.body;
                core_ptr->notify_cb(pane_ptr, notif.type, msg.c_str(),
                                    core_ptr->notify_user_data);
            }
        });

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

// ---------- Extended screen query ----------

const char* tc_pane_get_title(TermPane* pane) {
    if (!pane || !pane->screen) return "";
    pane->title_buf = pane->screen->title();
    return pane->title_buf.c_str();
}

const char* tc_pane_get_working_dir(TermPane* pane) {
    if (!pane || !pane->screen) return "";
    pane->cwd_buf = pane->screen->workingDirectory();
    return pane->cwd_buf.c_str();
}

int tc_pane_get_cursor_style(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return static_cast<int>(pane->screen->cursorShape());
}

int tc_pane_get_cursor_blink(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return pane->screen->cursorBlink() ? 1 : 0;
}

int tc_pane_scrollback_size(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return static_cast<int>(pane->screen->scrollbackSize());
}

const char* tc_pane_get_scrollback_line(TermPane* pane, int line) {
    if (!pane || !pane->screen) return "";
    pane->scrollback_buf = pane->screen->getScrollbackLineText(line);
    return pane->scrollback_buf.c_str();
}

int tc_pane_alt_screen_active(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return pane->screen->altScreenActive() ? 1 : 0;
}

int tc_pane_bracketed_paste(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return pane->screen->bracketedPaste() ? 1 : 0;
}

int tc_pane_app_cursor_keys(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return pane->screen->appCursorKeys() ? 1 : 0;
}

int tc_pane_mouse_mode_active(TermPane* pane) {
    if (!pane || !pane->screen) return 0;
    return pane->screen->mouseMode() != termcore::MouseMode::None ? 1 : 0;
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

// ---------- Mouse event encoding ----------

int tc_pane_encode_mouse(TermPane* pane, int type, int button,
                          int col, int row, int mods,
                          char* buf, size_t buf_size) {
    if (!pane || !pane->screen || !buf) return 0;
    MouseEvent event;
    event.type = static_cast<MouseEventType>(type);
    event.button = static_cast<MouseButton>(button);
    event.col = col;
    event.row = row;
    event.shift = (mods & 1) != 0;
    event.alt = (mods & 2) != 0;
    event.ctrl = (mods & 4) != 0;

    auto seq = encodeMouseEvent(event,
                                 pane->screen->mouseMode(),
                                 pane->screen->mouseEncoding());
    if (seq.empty() || seq.size() > buf_size) return 0;
    memcpy(buf, seq.data(), seq.size());
    return static_cast<int>(seq.size());
}

// ---------- Mux operations ----------

int tc_workspace_create(TermCore* core, const char* name) {
    if (!core) return 0;
    return static_cast<int>(core->mux.createWorkspace(name ? name : ""));
}

int tc_workspace_active(TermCore* core) {
    if (!core) return 0;
    return static_cast<int>(core->mux.activeWorkspaceId());
}

void tc_workspace_set_active(TermCore* core, int workspace_id) {
    if (!core) return;
    core->mux.setActiveWorkspace(static_cast<termcore::WorkspaceId>(workspace_id));
}

int tc_workspace_count(TermCore* core) {
    if (!core) return 0;
    return static_cast<int>(core->mux.workspaceCount());
}

int tc_tab_create(TermCore* core, int workspace_id) {
    if (!core) return 0;
    return static_cast<int>(
        core->mux.createTab(static_cast<termcore::WorkspaceId>(workspace_id)));
}

int tc_tab_active(TermCore* core, int workspace_id) {
    if (!core) return 0;
    auto* tab = core->mux.activeTab(
        static_cast<termcore::WorkspaceId>(workspace_id));
    return tab ? static_cast<int>(tab->id) : 0;
}

int tc_split_pane(TermCore* core, int workspace_id, int tab_id,
                   int pane_id, int direction) {
    if (!core) return 0;
    auto dir = direction == 0 ? termcore::SplitDirection::Horizontal
                              : termcore::SplitDirection::Vertical;
    return static_cast<int>(core->mux.splitPane(
        static_cast<termcore::WorkspaceId>(workspace_id),
        static_cast<termcore::TabId>(tab_id),
        static_cast<termcore::PaneId>(pane_id), dir));
}

void tc_close_pane_in_tab(TermCore* core, int workspace_id, int tab_id,
                           int pane_id) {
    if (!core) return;
    core->mux.closePane(static_cast<termcore::WorkspaceId>(workspace_id),
                        static_cast<termcore::TabId>(tab_id),
                        static_cast<termcore::PaneId>(pane_id));
}

int tc_pane_active_in_tab(TermCore* core, int workspace_id, int tab_id) {
    if (!core) return 0;
    return static_cast<int>(core->mux.activePaneId(
        static_cast<termcore::WorkspaceId>(workspace_id),
        static_cast<termcore::TabId>(tab_id)));
}

// ---------- Agent tracking ----------

int tc_agent_detect(TermCore* core, const char* process_name) {
    if (!core || !process_name) return 0;
    return static_cast<int>(core->agent_tracker.detectAgent(process_name));
}

void tc_agent_report_state(TermCore* core, int pane_id, int agent_type,
                            int state, const char* message) {
    if (!core) return;
    core->agent_tracker.reportState(
        static_cast<uint32_t>(pane_id),
        static_cast<termcore::AgentType>(agent_type),
        static_cast<termcore::AgentState>(state),
        message ? message : "");
}

void tc_agent_report_start(TermCore* core, int pane_id, int agent_type,
                            int pid) {
    if (!core) return;
    core->agent_tracker.reportStart(
        static_cast<uint32_t>(pane_id),
        static_cast<termcore::AgentType>(agent_type), pid);
}

void tc_agent_report_exit(TermCore* core, int pane_id) {
    if (!core) return;
    core->agent_tracker.reportExit(static_cast<uint32_t>(pane_id));
}

int tc_agent_get_state(TermCore* core, int pane_id) {
    if (!core) return 0;
    const auto* info = core->agent_tracker.getAgent(
        static_cast<uint32_t>(pane_id));
    return info ? static_cast<int>(info->state) : 0;
}

int tc_agent_any_needs_input(TermCore* core) {
    if (!core) return 0;
    return core->agent_tracker.anyNeedsInput() ? 1 : 0;
}

void tc_agent_sweep_stale(TermCore* core) {
    if (!core) return;
    core->agent_tracker.sweepStale();
}

// ---------- Lua plugin system ----------

int tc_lua_load_plugin(TermCore* core, const char* plugin_path) {
    if (!core || !plugin_path) return -1;
    return core->lua_engine.loadPlugin(plugin_path) ? 0 : -1;
}

int tc_lua_load_string(TermCore* core, const char* code) {
    if (!core || !code) return -1;
    return core->lua_engine.loadString(code) ? 0 : -1;
}

void tc_lua_fire_event(TermCore* core, int event_type, const char* data) {
    if (!core) return;
    core->lua_engine.fireEvent(static_cast<termcore::LuaEvent>(event_type),
                               data ? data : "");
}

// ---------- Notifications ----------

int tc_notification_count(TermCore* core) {
    if (!core) return 0;
    return static_cast<int>(core->notifications.count());
}

int tc_notification_unread_count(TermCore* core) {
    if (!core) return 0;
    return static_cast<int>(core->notifications.unreadCount());
}

void tc_notification_mark_all_read(TermCore* core) {
    if (!core) return;
    core->notifications.markAllRead();
}

void tc_notification_clear(TermCore* core) {
    if (!core) return;
    core->notifications.clear();
}

// ---------- Version ----------

const char* termcore_version(void) {
    return "0.1.0";
}

} // extern "C"
