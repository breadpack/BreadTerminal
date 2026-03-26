#include "termcore/screen.h"
#include "screen_colors.h"
#include <algorithm>

namespace termcore {

static int paramOr(const std::vector<VtParam>& params, size_t idx, int def) {
    if (idx < params.size() && params[idx].value > 0)
        return params[idx].value;
    return def;
}

// --- onCsiDispatch ---
void Screen::onCsiDispatch(char32_t final_char,
                           const std::vector<VtParam>& params,
                           const std::string& intermediates) {
    // SGR and mode changes should not clear wrap_pending state
    if (final_char != 'm' && final_char != 'h' && final_char != 'l') {
        wrap_pending_ = false;
    }

    switch (final_char) {
    case 'A': case 'B': case 'C': case 'D':
    case 'H': case 'f':
        handleCursorMovement(final_char, params);
        break;
    case 'J':
        handleEraseDisplay(params);
        break;
    case 'K':
        handleEraseLine(params);
        break;
    case 'm':
        handleSGR(params);
        break;
    case 'r':
        handleScrollRegion(params);
        break;
    case 'h': case 'l':
        handleMode(final_char, params, intermediates);
        break;
    case 'L': case 'M':
        handleInsertDeleteLines(final_char, params);
        break;
    case 'P': case '@':
        handleInsertDeleteChars(final_char, params);
        break;
    case 'S': case 'T':
        handleScrollUpDown(final_char, params);
        break;
    case 'X':
        handleEraseChars(params);
        break;
    case 'd': case 'G':
        handleAbsolutePosition(final_char, params);
        break;
    case 'n':  // DSR - Device Status Report
        handleDeviceStatusReport(params, intermediates);
        break;
    case 'c':  // DA - Device Attributes
        handleDeviceAttributes(params, intermediates);
        break;
    case 'q':  // DECSCUSR - Set Cursor Style, or XTVERSION
        if (intermediates.find('>') != std::string::npos) {
            // XTVERSION: CSI > q
            if (response_callback_) {
                response_callback_("\033P>|BreadTerminal 0.1\033\\");
            }
        } else {
            handleCursorStyle(params);
        }
        break;
    case 'b':  // REP - Repeat preceding character
        handleRepeatChar(params);
        break;
    case 'E':  // CNL - Cursor Next Line
    case 'F':  // CPL - Cursor Previous Line
        handleCursorNextPrevLine(final_char, params);
        break;
    case 'I':  // CHT - Cursor Horizontal Tab
    case 'Z':  // CBT - Cursor Backward Tab
        handleTabMovement(final_char, params);
        break;
    case 'g':  // TBC - Tab Clear
        handleTabClear(params);
        break;
    case 'p':  // DECRQM - Request Mode (with intermediate '$')
        if (intermediates.find('$') != std::string::npos) {
            handleModeQuery(params, intermediates);
        }
        break;
    case 'u':  // Kitty keyboard protocol
        if (intermediates == ">") {
            kitty_keyboard_.pushMode(paramOr(params, 0, 0));
            // ConPTY on Windows filters out CSI ?1049h (alt screen) but TUI apps
            // that enable kitty keyboard always use alt screen. Detect this and
            // switch to alt screen internally so old content is cleared.
            if (!alt_screen_active_) {
                switchToAltScreen(true);
            }
        } else if (intermediates == "<") {
            kitty_keyboard_.popMode(paramOr(params, 0, 1));
            // TUI app is exiting — restore primary screen
            if (kitty_keyboard_.currentFlags() == 0 && alt_screen_active_) {
                switchToPrimaryScreen(true);
            }
        } else if (intermediates == "?") {
            if (response_callback_) {
                response_callback_("\033[?" +
                    std::to_string(kitty_keyboard_.currentFlags()) + "u");
            }
        }
        break;
    default:
        break;
    }
}

// --- Cursor Movement ---
void Screen::handleCursorMovement(char32_t final_char,
                                  const std::vector<VtParam>& params) {
    int n = paramOr(params, 0, 1);

    switch (final_char) {
    case 'A': // CUU - cursor up
        cursor_.row = std::max(0, cursor_.row - n);
        break;
    case 'B': // CUD - cursor down
        cursor_.row = std::min(rows_ - 1, cursor_.row + n);
        break;
    case 'C': // CUF - cursor forward
        cursor_.col = std::min(cols_ - 1, cursor_.col + n);
        break;
    case 'D': // CUB - cursor back
        cursor_.col = std::max(0, cursor_.col - n);
        break;
    case 'H': // CUP - cursor position
    case 'f': { // HVP
        int row = paramOr(params, 0, 1) - 1; // 1-based to 0-based
        int col = paramOr(params, 1, 1) - 1;
        if (origin_mode_) {
            // In origin mode, coordinates are relative to scroll region
            row += scroll_top_;
            cursor_.row = std::clamp(row, scroll_top_, scroll_bottom_);
        } else {
            cursor_.row = std::clamp(row, 0, rows_ - 1);
        }
        cursor_.col = std::clamp(col, 0, cols_ - 1);
        break;
    }
    default:
        break;
    }
}

// --- Erase Display ---
void Screen::handleEraseDisplay(const std::vector<VtParam>& params) {
    int mode = paramOr(params, 0, 0);
    // When param is -1 (default), treat as 0
    if (params.empty() || params[0].value <= 0) mode = 0;

    switch (mode) {
    case 0: // Erase below (from cursor to end)
        for (int c = cursor_.col; c < cols_; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        markRowDirty(cursor_.row);
        for (int r = cursor_.row + 1; r < rows_; ++r) {
            for (int c = 0; c < cols_; ++c)
                eraseCell(mutableCellAt(r, c));
            markRowDirty(r);
        }
        break;
    case 1: // Erase above (from start to cursor)
        for (int r = 0; r < cursor_.row; ++r) {
            for (int c = 0; c < cols_; ++c)
                eraseCell(mutableCellAt(r, c));
            markRowDirty(r);
        }
        for (int c = 0; c <= cursor_.col; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        markRowDirty(cursor_.row);
        break;
    case 2: // Erase all
        for (int r = 0; r < rows_; ++r)
            for (int c = 0; c < cols_; ++c)
                eraseCell(mutableCellAt(r, c));
        markAllDirty();
        break;
    case 3: // Erase scrollback
        scrollback_.clear();
        break;
    default:
        break;
    }
}

// --- Erase Line ---
void Screen::handleEraseLine(const std::vector<VtParam>& params) {
    int mode = paramOr(params, 0, 0);
    if (params.empty() || params[0].value <= 0) mode = 0;

    switch (mode) {
    case 0: // Erase right
        for (int c = cursor_.col; c < cols_; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        markRowDirty(cursor_.row);
        break;
    case 1: // Erase left
        for (int c = 0; c <= cursor_.col; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        markRowDirty(cursor_.row);
        break;
    case 2: // Erase entire line
        for (int c = 0; c < cols_; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        markRowDirty(cursor_.row);
        break;
    default:
        break;
    }
}

/// Helper: parse an extended color from params starting at index i.
/// Handles both semicolon style (38;2;r;g;b, 38;5;n) and colon sub-param style (38:2::r:g:b, 38:5:n).
/// Returns the parsed color and advances i past the consumed params.
static uint32_t parseExtendedColor(const std::vector<VtParam>& params, size_t& i,
                                   const DynamicColors& dyn, bool& ok) {
    ok = false;
    const auto& p = params[i];

    // Colon sub-parameter style: 38:2::r:g:b or 38:5:n
    if (p.hasSub()) {
        int mode = p.subOr(0, -1);
        if (mode == 5) {
            // 38:5:n — indexed color
            int idx = p.subOr(1, -1);
            if (idx >= 0 && idx < 256) {
                ok = true;
                return dyn.palette[idx];
            }
        } else if (mode == 2) {
            // 38:2::r:g:b or 38:2:cs:r:g:b
            // Sub-params: [2, colorspace?, r, g, b]
            // If sub[1] == -1 (empty/omitted), it's the colorspace placeholder
            int r_idx = 1, g_idx = 2, b_idx = 3;
            if (p.sub.size() >= 5) {
                // 38:2:cs:r:g:b — skip colorspace
                r_idx = 2; g_idx = 3; b_idx = 4;
            } else if (p.sub.size() >= 4 && p.sub[1] == -1) {
                // 38:2::r:g:b — empty colorspace
                r_idx = 2; g_idx = 3; b_idx = 4;
            }
            if (static_cast<size_t>(b_idx) < p.sub.size()) {
                int r = p.sub[r_idx], g = p.sub[g_idx], b = p.sub[b_idx];
                if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
                ok = true;
                return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
            }
        }
        return 0;
    }

    // Semicolon style: 38;5;n or 38;2;r;g;b
    if (i + 1 < params.size()) {
        int sub = params[i + 1].value;
        if (sub == 5 && i + 2 < params.size()) {
            int idx = params[i + 2].value;
            if (idx >= 0 && idx < 256) {
                ok = true;
                i += 2;
                return dyn.palette[idx];
            }
            i += 2;
        } else if (sub == 2 && i + 4 < params.size()) {
            int r = params[i + 2].value, g = params[i + 3].value, b = params[i + 4].value;
            if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
            ok = true;
            i += 4;
            return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
        }
    }
    return 0;
}

// --- SGR ---
void Screen::handleSGR(const std::vector<VtParam>& params) {
    if (params.empty()) {
        // ESC[m = reset
        pen_ = Pen{};
        return;
    }

    for (size_t i = 0; i < params.size(); ++i) {
        int p = params[i].value;
        if (p <= 0) { // 0 or default (-1)
            pen_ = Pen{};
        } else if (p == 1) {
            pen_.attributes |= AttrBold;
        } else if (p == 2) {
            pen_.attributes |= AttrDim;
        } else if (p == 3) {
            pen_.attributes |= AttrItalic;
        } else if (p == 4) {
            // Underline: check for sub-parameters (4:0, 4:1, ..., 4:5)
            if (params[i].hasSub()) {
                int style = params[i].subOr(0, 1);
                if (style == 0) {
                    pen_.attributes &= ~AttrUnderline;
                    pen_.underline_style = UnderlineNone;
                } else if (style >= 1 && style <= 5) {
                    pen_.attributes |= AttrUnderline;
                    pen_.underline_style = static_cast<uint8_t>(style);
                }
            } else {
                pen_.attributes |= AttrUnderline;
                pen_.underline_style = UnderlineSingle;
            }
        } else if (p == 5) {
            pen_.attributes |= AttrBlink;
        } else if (p == 7) {
            pen_.attributes |= AttrInverse;
        } else if (p == 8) {
            pen_.attributes |= AttrHidden;
        } else if (p == 9) {
            pen_.attributes |= AttrStrikethrough;
        } else if (p == 21) {
            // SGR 21: double underline
            pen_.attributes |= AttrUnderline;
            pen_.underline_style = UnderlineDouble;
        } else if (p == 22) {
            pen_.attributes &= ~(AttrBold | AttrDim);
        } else if (p == 23) {
            pen_.attributes &= ~AttrItalic;
        } else if (p == 24) {
            pen_.attributes &= ~AttrUnderline;
            pen_.underline_style = UnderlineNone;
        } else if (p == 27) {
            pen_.attributes &= ~AttrInverse;
        } else if (p == 28) {
            pen_.attributes &= ~AttrHidden;
        } else if (p == 29) {
            pen_.attributes &= ~AttrStrikethrough;
        } else if (p >= 30 && p <= 37) {
            pen_.fg_color = dynamic_colors_.palette[p - 30];
        } else if (p == 38) {
            bool ok = false;
            uint32_t color = parseExtendedColor(params, i, dynamic_colors_, ok);
            if (ok) pen_.fg_color = color;
        } else if (p == 39) {
            pen_.fg_color = kColorDefault;
        } else if (p >= 40 && p <= 47) {
            pen_.bg_color = dynamic_colors_.palette[p - 40];
        } else if (p == 48) {
            bool ok = false;
            uint32_t color = parseExtendedColor(params, i, dynamic_colors_, ok);
            if (ok) pen_.bg_color = color;
        } else if (p == 49) {
            pen_.bg_color = kColorDefault;
        } else if (p == 58) {
            // Underline color: 58;2;r;g;b or 58:2::r:g:b or 58;5;n or 58:5:n
            bool ok = false;
            uint32_t color = parseExtendedColor(params, i, dynamic_colors_, ok);
            if (ok) pen_.underline_color = color;
        } else if (p == 59) {
            // Reset underline color to default
            pen_.underline_color = kColorDefault;
        } else if (p >= 90 && p <= 97) {
            pen_.fg_color = dynamic_colors_.palette[p - 90 + 8];
        } else if (p >= 100 && p <= 107) {
            pen_.bg_color = dynamic_colors_.palette[p - 100 + 8];
        }
    }
}

// --- Scroll Region ---
void Screen::handleScrollRegion(const std::vector<VtParam>& params) {
    int top = paramOr(params, 0, 1) - 1;
    int bottom = paramOr(params, 1, rows_) - 1;
    top = std::clamp(top, 0, rows_ - 1);
    bottom = std::clamp(bottom, 0, rows_ - 1);
    if (top < bottom) {
        scroll_top_ = top;
        scroll_bottom_ = bottom;
    }
    // CUP home after DECSTBM; in origin mode, home is scroll_top_
    if (origin_mode_) {
        cursor_.row = scroll_top_;
    } else {
        cursor_.row = 0;
    }
    cursor_.col = 0;
}

// --- Mode set/reset ---
void Screen::handleMode(char32_t final_char,
                        const std::vector<VtParam>& params,
                        const std::string& intermediates) {
    bool set = (final_char == 'h');
    bool is_private = (intermediates.find('?') != std::string::npos);

    for (const auto& vp : params) {
        int p = vp.value;

        if (!is_private) {
            // Non-private (ANSI) modes
            switch (p) {
            case 4:  // IRM - Insert/Replace Mode
                insert_mode_ = set;
                break;
            default:
                break;
            }
            continue;
        }

        switch (p) {
        case 1:    // DECCKM - Application cursor keys
            app_cursor_keys_ = set;
            break;
        case 6:    // DECOM - Origin mode
            origin_mode_ = set;
            // Reset cursor to home on mode change
            if (origin_mode_) {
                cursor_.row = scroll_top_;
            } else {
                cursor_.row = 0;
            }
            cursor_.col = 0;
            break;
        case 7:    // DECAWM - Auto-wrap mode
            autowrap_ = set;
            break;
        case 12:   // Cursor blink
            cursor_.blink = set;
            break;
        case 25:   // DECTCEM - Cursor visible
            cursor_.visible = set;
            break;
        case 47:   // Alternate screen (no save/restore cursor)
            if (set && !alt_screen_active_) switchToAltScreen(false);
            else if (!set && alt_screen_active_) switchToPrimaryScreen(false);
            break;
        case 1000: // Mouse: X10 basic click
            mouse_mode_ = set ? MouseMode::X10 : MouseMode::None;
            break;
        case 1002: // Mouse: button event tracking
            mouse_mode_ = set ? MouseMode::ButtonEvent : MouseMode::None;
            break;
        case 1003: // Mouse: any event tracking
            mouse_mode_ = set ? MouseMode::AnyEvent : MouseMode::None;
            break;
        case 1004: // Focus events
            focus_events_ = set;
            break;
        case 1006: // Mouse: SGR extended encoding
            mouse_encoding_ = set ? MouseEncoding::SGR : MouseEncoding::Default;
            break;
        case 1047: // Alternate screen (clear on enter)
            if (set && !alt_screen_active_) {
                switchToAltScreen(false);
                clearScreen();
            } else if (!set && alt_screen_active_) {
                switchToPrimaryScreen(false);
            }
            break;
        case 1049: // Alternate screen + save/restore cursor
            if (set && !alt_screen_active_) switchToAltScreen(true);
            else if (!set && alt_screen_active_) switchToPrimaryScreen(true);
            break;
        case 2004: // Bracketed paste mode
            bracketed_paste_ = set;
            break;
        case 2026: // Synchronized output
            sync_update_ = set;
            if (set) {
                sync_start_time_ = std::chrono::steady_clock::now();
            } else {
                // ESU received: mark all rows dirty so the deferred frame renders fully
                markAllDirty();
            }
            break;
        default:
            break;
        }
    }
}

// --- Insert/Delete Lines ---
void Screen::handleInsertDeleteLines(char32_t final_char,
                                     const std::vector<VtParam>& params) {
    int n = paramOr(params, 0, 1);

    if (cursor_.row < scroll_top_ || cursor_.row > scroll_bottom_)
        return;

    if (final_char == 'L') { // IL - insert lines
        scrollDown(cursor_.row, scroll_bottom_, n);
    } else { // 'M' - DL - delete lines
        scrollUp(cursor_.row, scroll_bottom_, n);
    }
}

// --- Insert/Delete Characters ---
void Screen::handleInsertDeleteChars(char32_t final_char,
                                     const std::vector<VtParam>& params) {
    int n = paramOr(params, 0, 1);
    auto& row = grid_[cursor_.row];

    if (final_char == '@') { // ICH - insert characters
        n = std::min(n, cols_ - cursor_.col);
        for (int i = 0; i < n; ++i) {
            row.insert(row.begin() + cursor_.col, TermCell{});
        }
        row.resize(cols_);
    } else { // 'P' - DCH - delete characters
        n = std::min(n, cols_ - cursor_.col);
        row.erase(row.begin() + cursor_.col,
                  row.begin() + cursor_.col + n);
        row.resize(cols_);
    }
    markRowDirty(cursor_.row);
}

// --- Scroll Up/Down ---
void Screen::handleScrollUpDown(char32_t final_char,
                                const std::vector<VtParam>& params) {
    int n = paramOr(params, 0, 1);
    if (final_char == 'S') { // SU
        scrollUp(scroll_top_, scroll_bottom_, n);
    } else { // 'T' - SD
        scrollDown(scroll_top_, scroll_bottom_, n);
    }
}

// --- Erase Characters ---
void Screen::handleEraseChars(const std::vector<VtParam>& params) {
    int n = paramOr(params, 0, 1);
    n = std::min(n, cols_ - cursor_.col);
    for (int i = 0; i < n; ++i) {
        eraseCell(mutableCellAt(cursor_.row, cursor_.col + i));
    }
    markRowDirty(cursor_.row);
}

// --- Absolute Position ---
void Screen::handleAbsolutePosition(char32_t final_char,
                                    const std::vector<VtParam>& params) {
    int val = paramOr(params, 0, 1) - 1;
    if (final_char == 'd') { // VPA - cursor to absolute row
        if (origin_mode_) {
            val += scroll_top_;
            cursor_.row = std::clamp(val, scroll_top_, scroll_bottom_);
        } else {
            cursor_.row = std::clamp(val, 0, rows_ - 1);
        }
    } else { // 'G' - CHA - cursor to absolute column
        cursor_.col = std::clamp(val, 0, cols_ - 1);
    }
}

} // namespace termcore
