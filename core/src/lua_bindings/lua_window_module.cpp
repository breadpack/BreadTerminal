// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_window_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_window_module.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace termcore {

// Helper: cast shared_ptr<void> to shared_ptr<sol::protected_function>
static auto asFn(const std::shared_ptr<void>& p)
    -> std::shared_ptr<sol::protected_function> {
    return std::static_pointer_cast<sol::protected_function>(p);
}

static std::shared_ptr<void> makeFn(sol::protected_function fn) {
    return std::make_shared<sol::protected_function>(std::move(fn));
}

// ---------------------------------------------------------------------------
// Helper: parse "#rrggbb" hex colour string to uint32_t.
// ---------------------------------------------------------------------------
static uint32_t parseHexColor(const std::string& s, uint32_t fallback) {
    if (s.size() != 7 || s[0] != '#') return fallback;
    try {
        return static_cast<uint32_t>(std::stoul(s.substr(1), nullptr, 16));
    } catch (...) {
        return fallback;
    }
}

// ---------------------------------------------------------------------------
LuaWindowModule::LuaWindowModule() = default;

const std::vector<std::shared_ptr<FloatingWindowData>>& LuaWindowModule::windows() const {
    return windows_;
}

bool LuaWindowModule::hasOpenWindows() const {
    return std::any_of(windows_.begin(), windows_.end(),
                       [](const std::shared_ptr<FloatingWindowData>& w) { return w->open; });
}

void LuaWindowModule::dispatchKey(uint64_t window_id, const std::string& key) {
    for (auto& wp : windows_) {
        if (wp->id == window_id && wp->open) {
            auto it = wp->key_handlers.find(key);
            if (it != wp->key_handlers.end() && it->second) {
                auto fn = asFn(it->second);
                auto result = (*fn)();
                (void)result;
            }
            return;
        }
    }
}

void LuaWindowModule::closeWindow(uint64_t window_id) {
    for (auto& wp : windows_) {
        if (wp->id == window_id && wp->open) {
            wp->open = false;
            if (wp->on_close_cb) {
                auto fn = asFn(wp->on_close_cb);
                auto result = (*fn)();
                (void)result;
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
void LuaWindowModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    // ---- register FloatingWindow usertype --------------------------------
    lua.new_usertype<FloatingWindowData>("FloatingWindow",
        sol::no_constructor,

        "set_lines", [](FloatingWindowData& self, sol::table lines,
                        sol::optional<int> start_row_opt) {
            if (!self.open) return;
            int start = start_row_opt.value_or(0);
            if (start < 0) start = 0;

            int idx = start;
            for (auto& kv : lines) {
                std::string line = kv.second.as<std::string>();
                if (idx < static_cast<int>(self.lines.size())) {
                    self.lines[static_cast<size_t>(idx)] = std::move(line);
                } else {
                    self.lines.push_back(std::move(line));
                }
                ++idx;
            }
        },

        "get_lines", [](FloatingWindowData& self, sol::this_state ts) -> sol::table {
            sol::state_view lua_sv(ts);
            sol::table t = lua_sv.create_table();
            for (size_t i = 0; i < self.lines.size(); ++i) {
                t[static_cast<int>(i + 1)] = self.lines[i];
            }
            return t;
        },

        "get_line", [](FloatingWindowData& self, int row) -> std::string {
            if (row < 0 || row >= static_cast<int>(self.lines.size())) return "";
            return self.lines[static_cast<size_t>(row)];
        },

        "set_cursor", [](FloatingWindowData& self, int row, int col) {
            if (!self.open) return;
            self.cursor_row = row;
            self.cursor_col = col;
        },

        "get_cursor", [](FloatingWindowData& self) -> std::tuple<int, int> {
            return {self.cursor_row, self.cursor_col};
        },

        "move_cursor", [](FloatingWindowData& self, int dr, int dc) {
            if (!self.open) return;
            self.cursor_row += dr;
            self.cursor_col += dc;
            if (self.cursor_row < 0) self.cursor_row = 0;
            if (self.cursor_col < 0) self.cursor_col = 0;
        },

        "set_highlight", [](FloatingWindowData& self, int row, int sc, int ec,
                            sol::table opts) {
            if (!self.open) return;
            HighlightRange hl;
            hl.row = row;
            hl.start_col = sc;
            hl.end_col = ec;
            hl.fg = parseHexColor(opts.get_or<std::string>("fg", ""), 0xffffff);
            hl.bg = parseHexColor(opts.get_or<std::string>("bg", ""), 0x000000);
            hl.bold = opts.get_or("bold", false);
            hl.italic = opts.get_or("italic", false);
            hl.underline = opts.get_or("underline", false);
            self.highlights.push_back(hl);
        },

        "on_key", [](FloatingWindowData& self, std::string key,
                     sol::protected_function fn) {
            self.key_handlers[key] = makeFn(std::move(fn));
        },

        "scroll_to", [](FloatingWindowData& self, int row) {
            if (!self.open) return;
            self.scroll_offset = row;
            if (self.on_scroll_cb) {
                auto fn = asFn(self.on_scroll_cb);
                auto r = (*fn)(self.scroll_offset);
                (void)r;
            }
        },

        "get_scroll_offset", [](FloatingWindowData& self) -> int {
            return self.scroll_offset;
        },

        "is_open", [](FloatingWindowData& self) -> bool {
            return self.open;
        },

        "set_title", [](FloatingWindowData& self, std::string title) {
            if (!self.open) return;
            self.title = std::move(title);
        },

        "resize", [](FloatingWindowData& self, int w, int h) {
            if (!self.open) return;
            self.width = w;
            self.height = h;
        },

        "close", [](FloatingWindowData& self) {
            if (!self.open) return;
            self.open = false;
            if (self.on_close_cb) {
                auto fn = asFn(self.on_close_cb);
                auto r = (*fn)();
                (void)r;
            }
        },

        "on_close", [](FloatingWindowData& self, sol::protected_function fn) {
            self.on_close_cb = makeFn(std::move(fn));
        },

        "on_scroll", [](FloatingWindowData& self, sol::protected_function fn) {
            self.on_scroll_cb = makeFn(std::move(fn));
        }
    );

    // ---- terminal.window sub-table --------------------------------------
    auto win = terminal.create_named("window");

    win.set_function("open",
        [this](sol::table opts) -> std::shared_ptr<FloatingWindowData> {
            auto w = std::make_shared<FloatingWindowData>();
            w->id = next_id_++;
            w->width = opts.get_or("width", 60);
            w->height = opts.get_or("height", 20);
            w->title = opts.get_or<std::string>("title", "");
            w->border_style = opts.get_or<std::string>("border", "rounded");
            w->relative = opts.get_or<std::string>("relative", "editor");
            w->anchor = opts.get_or<std::string>("anchor", "center");
            w->focusable = opts.get_or("focusable", true);

            sol::optional<sol::table> style_opt =
                opts.get<sol::optional<sol::table>>("style");
            if (style_opt) {
                auto& st = *style_opt;
                w->style.background =
                    parseHexColor(st.get_or<std::string>("background", ""), 0x1e1e2e);
                w->style.foreground =
                    parseHexColor(st.get_or<std::string>("foreground", ""), 0xcdd6f4);
                w->style.border_color =
                    parseHexColor(st.get_or<std::string>("border_color", ""), 0x89b4fa);
                w->style.title_color =
                    parseHexColor(st.get_or<std::string>("title_color", ""), 0xf5e0dc);
            }

            // Enforce window limit
            if (windows_.size() >= kMaxWindows) {
                // Remove oldest closed windows first
                removeClosedWindows();
                if (windows_.size() >= kMaxWindows) {
                    throw std::runtime_error("window.open: maximum window limit reached ("
                        + std::to_string(kMaxWindows) + ")");
                }
            }

            windows_.push_back(w);
            return w;
        });
}

void LuaWindowModule::removeClosedWindows() {
    windows_.erase(
        std::remove_if(windows_.begin(), windows_.end(),
                       [](const std::shared_ptr<FloatingWindowData>& w) {
                           return !w->open;
                       }),
        windows_.end());
}

// ---------------------------------------------------------------------------
void LuaWindowModule::clearCallbacks() {
    for (auto& wp : windows_) {
        if (wp->open) {
            wp->open = false;
            if (wp->on_close_cb) {
                auto fn = asFn(wp->on_close_cb);
                auto r = (*fn)();
                (void)r;
            }
        }
        wp->key_handlers.clear();
        wp->on_close_cb.reset();
        wp->on_scroll_cb.reset();
    }
    windows_.clear();
}

} // namespace termcore
