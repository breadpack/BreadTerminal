// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_pane_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_pane_module.h"

#include <algorithm>
#include <string>
#include <tuple>

namespace termcore {

// Static empty containers for safe const-ref returns
const std::vector<VirtualTextEntry> LuaPaneModule::empty_virtual_texts_;
const std::vector<HighlightEntry> LuaPaneModule::empty_highlights_;
const std::unordered_map<int, LineSign> LuaPaneModule::empty_line_signs_;

// ---- Color parsing helper ----

static uint32_t parseColor(const sol::object& obj) {
    if (obj.is<std::string>()) {
        std::string s = obj.as<std::string>();
        if (!s.empty() && s[0] == '#') {
            s = s.substr(1);
        }
        try {
            return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
        } catch (...) {
            return 0;
        }
    } else if (obj.is<int>()) {
        return static_cast<uint32_t>(obj.as<int>());
    }
    return 0;
}

// ---- LuaPaneHandle — Lua usertype that wraps a pane_id ----

struct LuaPaneHandle {
    uint32_t pane_id;
    LuaPaneModule* module;

    uint32_t id() const { return pane_id; }

    int rows() const {
        if (!module || !module->provider()) return 0;
        auto panes = module->provider()->listPanes();
        for (auto& p : panes) {
            if (p.id == pane_id) return p.rows;
        }
        return 0;
    }

    int cols() const {
        if (!module || !module->provider()) return 0;
        auto panes = module->provider()->listPanes();
        for (auto& p : panes) {
            if (p.id == pane_id) return p.cols;
        }
        return 0;
    }

    std::string title() const {
        if (!module || !module->provider()) return "";
        auto panes = module->provider()->listPanes();
        for (auto& p : panes) {
            if (p.id == pane_id) return p.title;
        }
        return "";
    }

    std::string cwd() const {
        if (!module || !module->provider()) return "";
        auto panes = module->provider()->listPanes();
        for (auto& p : panes) {
            if (p.id == pane_id) return p.cwd;
        }
        return "";
    }

    std::string process() const {
        if (!module || !module->provider()) return "";
        auto panes = module->provider()->listPanes();
        for (auto& p : panes) {
            if (p.id == pane_id) return p.process;
        }
        return "";
    }

    bool is_active() const {
        if (!module || !module->provider()) return false;
        auto panes = module->provider()->listPanes();
        for (auto& p : panes) {
            if (p.id == pane_id) return p.is_active;
        }
        return false;
    }

    // Read buffer content (1-indexed Lua -> 0-indexed C++)
    sol::object get_lines(sol::this_state L, int start_row, int end_row) const {
        if (!module || !module->provider()) return sol::make_object(L, sol::nil);
        auto lines = module->provider()->getPaneLines(pane_id, start_row - 1, end_row - 1);
        sol::state_view lua(L);
        sol::table tbl = lua.create_table();
        for (size_t i = 0; i < lines.size(); ++i) {
            tbl[static_cast<int>(i) + 1] = lines[i];
        }
        return sol::make_object(L, tbl);
    }

    std::string get_line(int row) const {
        if (!module || !module->provider()) return "";
        return module->provider()->getPaneLine(pane_id, row - 1);
    }

    std::string get_text(int row, int start_col, int end_col) const {
        if (!module || !module->provider()) return "";
        std::string line = module->provider()->getPaneLine(pane_id, row - 1);
        int s = start_col - 1;
        int e = end_col - 1;
        if (s < 0) s = 0;
        if (e >= static_cast<int>(line.size())) e = static_cast<int>(line.size()) - 1;
        if (s > e || s >= static_cast<int>(line.size())) return "";
        return line.substr(s, e - s + 1);
    }

    sol::object get_visible_lines(sol::this_state L) const {
        if (!module || !module->provider()) return sol::make_object(L, sol::nil);
        // Get pane dimensions to know how many rows
        int r = rows();
        if (r <= 0) return sol::make_object(L, sol::nil);
        auto lines = module->provider()->getPaneLines(pane_id, 0, r - 1);
        sol::state_view lua(L);
        sol::table tbl = lua.create_table();
        for (size_t i = 0; i < lines.size(); ++i) {
            tbl[static_cast<int>(i) + 1] = lines[i];
        }
        return sol::make_object(L, tbl);
    }

    int scrollback_size() const {
        if (!module || !module->provider()) return 0;
        return module->provider()->getScrollbackSize(pane_id);
    }

    sol::object get_scrollback(sol::this_state L, int start, int count) const {
        if (!module || !module->provider()) return sol::make_object(L, sol::nil);
        auto lines = module->provider()->getScrollbackLines(pane_id, start - 1, count);
        sol::state_view lua(L);
        sol::table tbl = lua.create_table();
        for (size_t i = 0; i < lines.size(); ++i) {
            tbl[static_cast<int>(i) + 1] = lines[i];
        }
        return sol::make_object(L, tbl);
    }

    // Cursor (1-indexed returns)
    std::tuple<int, int> cursor() const {
        if (!module || !module->provider()) return {0, 0};
        auto [r, c] = module->provider()->getPaneCursor(pane_id);
        return {r + 1, c + 1};
    }

    // Selection
    sol::object get_selection(sol::this_state L) const {
        if (!module || !module->provider()) return sol::make_object(L, sol::nil);
        std::string sel = module->provider()->getPaneSelection(pane_id);
        if (sel.empty()) return sol::make_object(L, sol::nil);
        return sol::make_object(L, sel);
    }

    sol::object selection_range(sol::this_state L) const {
        if (!module || !module->provider()) return sol::make_object(L, sol::nil);
        std::string sel = module->provider()->getPaneSelection(pane_id);
        if (sel.empty()) return sol::make_object(L, sol::nil);
        auto [sr, sc] = module->provider()->getSelectionStart(pane_id);
        auto [er, ec] = module->provider()->getSelectionEnd(pane_id);
        // Return as multiple values via tuple (1-indexed)
        return sol::make_object(L,
            std::make_tuple(sr + 1, sc + 1, er + 1, ec + 1));
    }

    // Send text / keys (PaneWrite)
    void send_text(const std::string& text) const {
        if (!module || !module->provider()) return;
        module->provider()->sendText(pane_id, text);
    }

    void send_keys(const std::string& keys) const {
        if (!module || !module->provider()) return;
        module->provider()->sendKeys(pane_id, keys);
    }

    // Virtual text
    uint64_t add_virtual_text(int row, int col, const std::string& text,
                              sol::optional<sol::table> opts) const {
        if (!module) return 0;
        uint32_t fg = 0, bg = 0;
        bool bold = false, italic = false, underline = false;
        if (opts) {
            sol::object fgObj = (*opts)["fg"];
            if (fgObj.valid()) fg = parseColor(fgObj);
            sol::object bgObj = (*opts)["bg"];
            if (bgObj.valid()) bg = parseColor(bgObj);
            bold = opts->get_or("bold", false);
            italic = opts->get_or("italic", false);
            underline = opts->get_or("underline", false);
        }
        return module->addVirtualText(pane_id, row - 1, col - 1, text, fg, bg,
                                      bold, italic, underline);
    }

    void remove_virtual_text(uint64_t mark_id) const {
        if (!module) return;
        module->removeVirtualText(pane_id, mark_id);
    }

    void clear_virtual_text() const {
        if (!module) return;
        module->clearVirtualText(pane_id);
    }

    // Highlights
    uint64_t add_highlight(int row, int start_col, int end_col,
                           sol::optional<sol::table> opts) const {
        if (!module) return 0;
        uint32_t fg = 0, bg = 0;
        bool bold = false, italic = false, underline = false;
        if (opts) {
            sol::object fgObj = (*opts)["fg"];
            if (fgObj.valid()) fg = parseColor(fgObj);
            sol::object bgObj = (*opts)["bg"];
            if (bgObj.valid()) bg = parseColor(bgObj);
            bold = opts->get_or("bold", false);
            italic = opts->get_or("italic", false);
            underline = opts->get_or("underline", false);
        }
        return module->addHighlight(pane_id, row - 1, start_col - 1, end_col - 1,
                                    fg, bg, bold, italic, underline);
    }

    void remove_highlight(uint64_t mark_id) const {
        if (!module) return;
        module->removeHighlight(pane_id, mark_id);
    }

    void clear_highlights() const {
        if (!module) return;
        module->clearHighlights(pane_id);
    }

    // Line signs
    void set_line_sign(int row, sol::table opts) const {
        if (!module) return;
        std::string text = opts.get_or<std::string>("text", "");
        if (text.size() > 2) text = text.substr(0, 2);
        uint32_t color = 0;
        sol::object colorObj = opts["color"];
        if (colorObj.valid()) color = parseColor(colorObj);
        module->setLineSign(pane_id, row - 1, text, color);
    }

    void remove_line_sign(int row) const {
        if (!module) return;
        module->removeLineSign(pane_id, row - 1);
    }

    // Events
    void on_output(sol::protected_function fn) const {
        if (!module) return;
        auto shared = std::make_shared<sol::protected_function>(std::move(fn));
        module->addOnOutput(pane_id, std::static_pointer_cast<void>(shared));
    }

    void on_exit(sol::protected_function fn) const {
        if (!module) return;
        auto shared = std::make_shared<sol::protected_function>(std::move(fn));
        module->addOnExit(pane_id, std::static_pointer_cast<void>(shared));
    }
};

// ---- LuaPaneModule implementation ----

LuaPaneModule::LuaPaneModule() = default;

void LuaPaneModule::setProvider(IPaneDataProvider* provider) {
    provider_ = provider;
}

void LuaPaneModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    // Register the PaneHandle usertype
    lua.new_usertype<LuaPaneHandle>("PaneHandle",
        sol::no_constructor,
        "id", &LuaPaneHandle::id,
        "rows", &LuaPaneHandle::rows,
        "cols", &LuaPaneHandle::cols,
        "title", &LuaPaneHandle::title,
        "cwd", &LuaPaneHandle::cwd,
        "process", &LuaPaneHandle::process,
        "is_active", &LuaPaneHandle::is_active,
        "get_lines", &LuaPaneHandle::get_lines,
        "get_line", &LuaPaneHandle::get_line,
        "get_text", &LuaPaneHandle::get_text,
        "get_visible_lines", &LuaPaneHandle::get_visible_lines,
        "scrollback_size", &LuaPaneHandle::scrollback_size,
        "get_scrollback", &LuaPaneHandle::get_scrollback,
        "cursor", &LuaPaneHandle::cursor,
        "get_selection", &LuaPaneHandle::get_selection,
        "selection_range", &LuaPaneHandle::selection_range,
        "send_text", &LuaPaneHandle::send_text,
        "send_keys", &LuaPaneHandle::send_keys,
        "add_virtual_text", &LuaPaneHandle::add_virtual_text,
        "remove_virtual_text", &LuaPaneHandle::remove_virtual_text,
        "clear_virtual_text", &LuaPaneHandle::clear_virtual_text,
        "add_highlight", &LuaPaneHandle::add_highlight,
        "remove_highlight", &LuaPaneHandle::remove_highlight,
        "clear_highlights", &LuaPaneHandle::clear_highlights,
        "set_line_sign", &LuaPaneHandle::set_line_sign,
        "remove_line_sign", &LuaPaneHandle::remove_line_sign,
        "on_output", &LuaPaneHandle::on_output,
        "on_exit", &LuaPaneHandle::on_exit
    );

    // Create terminal.pane sub-table
    auto paneTbl = terminal.create_named("pane");

    // terminal.pane.active() -> PaneHandle
    paneTbl.set_function("active",
        [this]() -> sol::optional<LuaPaneHandle> {
            if (!provider_) return sol::nullopt;
            auto info = provider_->getActivePane();
            if (info.id == 0) return sol::nullopt;
            return LuaPaneHandle{info.id, this};
        });

    // terminal.pane.get(pane_id) -> PaneHandle
    paneTbl.set_function("get",
        [this](uint32_t pane_id) -> sol::optional<LuaPaneHandle> {
            if (!provider_) return sol::nullopt;
            auto panes = provider_->listPanes();
            for (auto& p : panes) {
                if (p.id == pane_id) {
                    return LuaPaneHandle{pane_id, this};
                }
            }
            return sol::nullopt;
        });

    // terminal.pane.list() -> array of PaneHandles
    paneTbl.set_function("list",
        [this](sol::this_state L) -> sol::object {
            if (!provider_) return sol::make_object(L, sol::nil);
            sol::state_view lua(L);
            auto panes = provider_->listPanes();
            sol::table tbl = lua.create_table();
            for (size_t i = 0; i < panes.size(); ++i) {
                tbl[static_cast<int>(i) + 1] = LuaPaneHandle{panes[i].id, this};
            }
            return sol::make_object(L, tbl);
        });
}

void LuaPaneModule::clearCallbacks() {
    callbacks_.clear();
    overlays_.clear();
    next_mark_id_ = 1;
}

// ---- Overlay queries ----

const std::vector<VirtualTextEntry>& LuaPaneModule::virtualTexts(uint32_t pane_id) const {
    auto it = overlays_.find(pane_id);
    if (it == overlays_.end()) return empty_virtual_texts_;
    return it->second.virtual_texts;
}

const std::vector<HighlightEntry>& LuaPaneModule::highlights(uint32_t pane_id) const {
    auto it = overlays_.find(pane_id);
    if (it == overlays_.end()) return empty_highlights_;
    return it->second.highlights;
}

const std::unordered_map<int, LineSign>& LuaPaneModule::lineSigns(uint32_t pane_id) const {
    auto it = overlays_.find(pane_id);
    if (it == overlays_.end()) return empty_line_signs_;
    return it->second.line_signs;
}

// ---- Overlay mutation ----

uint64_t LuaPaneModule::addVirtualText(uint32_t pane_id, int row, int col,
                                        const std::string& text, uint32_t fg,
                                        uint32_t bg, bool bold, bool italic,
                                        bool underline) {
    uint64_t mid = next_mark_id_++;
    VirtualTextEntry entry{mid, row, col, text, fg, bg, bold, italic, underline};
    overlays_[pane_id].virtual_texts.push_back(std::move(entry));
    return mid;
}

bool LuaPaneModule::removeVirtualText(uint32_t pane_id, uint64_t mark_id) {
    auto it = overlays_.find(pane_id);
    if (it == overlays_.end()) return false;
    auto& vec = it->second.virtual_texts;
    auto vit = std::find_if(vec.begin(), vec.end(),
        [mark_id](const VirtualTextEntry& e) { return e.id == mark_id; });
    if (vit == vec.end()) return false;
    vec.erase(vit);
    return true;
}

void LuaPaneModule::clearVirtualText(uint32_t pane_id) {
    auto it = overlays_.find(pane_id);
    if (it != overlays_.end()) {
        it->second.virtual_texts.clear();
    }
}

uint64_t LuaPaneModule::addHighlight(uint32_t pane_id, int row, int start_col,
                                      int end_col, uint32_t fg, uint32_t bg,
                                      bool bold, bool italic, bool underline) {
    uint64_t mid = next_mark_id_++;
    HighlightEntry entry{mid, row, start_col, end_col, fg, bg, bold, italic, underline};
    overlays_[pane_id].highlights.push_back(std::move(entry));
    return mid;
}

bool LuaPaneModule::removeHighlight(uint32_t pane_id, uint64_t mark_id) {
    auto it = overlays_.find(pane_id);
    if (it == overlays_.end()) return false;
    auto& vec = it->second.highlights;
    auto vit = std::find_if(vec.begin(), vec.end(),
        [mark_id](const HighlightEntry& e) { return e.id == mark_id; });
    if (vit == vec.end()) return false;
    vec.erase(vit);
    return true;
}

void LuaPaneModule::clearHighlights(uint32_t pane_id) {
    auto it = overlays_.find(pane_id);
    if (it != overlays_.end()) {
        it->second.highlights.clear();
    }
}

void LuaPaneModule::setLineSign(uint32_t pane_id, int row,
                                 const std::string& text, uint32_t color) {
    LineSign sign{text, color};
    overlays_[pane_id].line_signs[row] = std::move(sign);
}

void LuaPaneModule::removeLineSign(uint32_t pane_id, int row) {
    auto it = overlays_.find(pane_id);
    if (it != overlays_.end()) {
        it->second.line_signs.erase(row);
    }
}

// ---- Callback registration ----

void LuaPaneModule::addOnOutput(uint32_t pane_id, std::shared_ptr<void> fn) {
    callbacks_[pane_id].on_output.push_back(std::move(fn));
}

void LuaPaneModule::addOnExit(uint32_t pane_id, std::shared_ptr<void> fn) {
    callbacks_[pane_id].on_exit.push_back(std::move(fn));
}

// ---- Event dispatch ----

void LuaPaneModule::fireOutput(uint32_t pane_id, const std::string& text) {
    auto it = callbacks_.find(pane_id);
    if (it == callbacks_.end()) return;
    for (auto& fn_void : it->second.on_output) {
        if (!fn_void) continue;
        auto fn = std::static_pointer_cast<sol::protected_function>(fn_void);
        auto result = (*fn)(text);
        (void)result;
    }
}

void LuaPaneModule::fireExit(uint32_t pane_id, int exit_code) {
    auto it = callbacks_.find(pane_id);
    if (it == callbacks_.end()) return;
    for (auto& fn_void : it->second.on_exit) {
        if (!fn_void) continue;
        auto fn = std::static_pointer_cast<sol::protected_function>(fn_void);
        auto result = (*fn)(exit_code);
        (void)result;
    }
}

} // namespace termcore
