#include "termcore/screen.h"
#include "screen_colors.h"
#include <algorithm>

namespace termcore {

static int paramOr(const std::vector<int>& params, size_t idx, int def) {
    if (idx < params.size() && params[idx] > 0)
        return params[idx];
    return def;
}

// --- onCsiDispatch ---
void Screen::onCsiDispatch(char32_t final_char,
                           const std::vector<int>& params,
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
    default:
        break;
    }
}

// --- Cursor Movement ---
void Screen::handleCursorMovement(char32_t final_char,
                                  const std::vector<int>& params) {
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
    case 'f': {
        int row = paramOr(params, 0, 1) - 1; // 1-based to 0-based
        int col = paramOr(params, 1, 1) - 1;
        cursor_.row = std::clamp(row, 0, rows_ - 1);
        cursor_.col = std::clamp(col, 0, cols_ - 1);
        break;
    }
    default:
        break;
    }
}

// --- Erase Display ---
void Screen::handleEraseDisplay(const std::vector<int>& params) {
    int mode = paramOr(params, 0, 0);
    // When param is -1 (default), treat as 0
    if (params.empty() || params[0] <= 0) mode = 0;

    switch (mode) {
    case 0: // Erase below (from cursor to end)
        for (int c = cursor_.col; c < cols_; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        for (int r = cursor_.row + 1; r < rows_; ++r)
            for (int c = 0; c < cols_; ++c)
                eraseCell(mutableCellAt(r, c));
        break;
    case 1: // Erase above (from start to cursor)
        for (int r = 0; r < cursor_.row; ++r)
            for (int c = 0; c < cols_; ++c)
                eraseCell(mutableCellAt(r, c));
        for (int c = 0; c <= cursor_.col; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        break;
    case 2: // Erase all
        for (int r = 0; r < rows_; ++r)
            for (int c = 0; c < cols_; ++c)
                eraseCell(mutableCellAt(r, c));
        break;
    case 3: // Erase scrollback
        scrollback_.clear();
        break;
    default:
        break;
    }
}

// --- Erase Line ---
void Screen::handleEraseLine(const std::vector<int>& params) {
    int mode = paramOr(params, 0, 0);
    if (params.empty() || params[0] <= 0) mode = 0;

    switch (mode) {
    case 0: // Erase right
        for (int c = cursor_.col; c < cols_; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        break;
    case 1: // Erase left
        for (int c = 0; c <= cursor_.col; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        break;
    case 2: // Erase entire line
        for (int c = 0; c < cols_; ++c)
            eraseCell(mutableCellAt(cursor_.row, c));
        break;
    default:
        break;
    }
}

// --- SGR ---
void Screen::handleSGR(const std::vector<int>& params) {
    if (params.empty()) {
        // ESC[m = reset
        pen_ = Pen{};
        return;
    }

    for (size_t i = 0; i < params.size(); ++i) {
        int p = params[i];
        if (p <= 0) { // 0 or default (-1)
            pen_ = Pen{};
        } else if (p == 1) {
            pen_.attributes |= AttrBold;
        } else if (p == 3) {
            pen_.attributes |= AttrItalic;
        } else if (p == 4) {
            pen_.attributes |= AttrUnderline;
        } else if (p == 7) {
            pen_.attributes |= AttrInverse;
        } else if (p == 8) {
            pen_.attributes |= AttrHidden;
        } else if (p == 9) {
            pen_.attributes |= AttrStrikethrough;
        } else if (p == 22) {
            pen_.attributes &= ~AttrBold;
        } else if (p == 23) {
            pen_.attributes &= ~AttrItalic;
        } else if (p == 24) {
            pen_.attributes &= ~AttrUnderline;
        } else if (p == 27) {
            pen_.attributes &= ~AttrInverse;
        } else if (p == 28) {
            pen_.attributes &= ~AttrHidden;
        } else if (p == 29) {
            pen_.attributes &= ~AttrStrikethrough;
        } else if (p >= 30 && p <= 37) {
            pen_.fg_color = kColorTable[p - 30];
        } else if (p == 38) {
            // Extended foreground: 38;5;n or 38;2;r;g;b
            if (i + 1 < params.size()) {
                int sub = params[i + 1];
                if (sub == 5 && i + 2 < params.size()) {
                    int idx = params[i + 2];
                    if (idx >= 0 && idx < 16)
                        pen_.fg_color = kColorTable[idx];
                    else if (idx >= 16 && idx < 232) {
                        int c = idx - 16;
                        int r = c / 36, g = (c / 6) % 6, b = c % 6;
                        pen_.fg_color = (kCubeValues[r] << 16) | (kCubeValues[g] << 8) | kCubeValues[b];
                    } else if (idx >= 232 && idx < 256) {
                        int gray = 8 + (idx - 232) * 10;
                        pen_.fg_color = (gray << 16) | (gray << 8) | gray;
                    }
                    i += 2;
                } else if (sub == 2 && i + 4 < params.size()) {
                    int r = params[i + 2], g = params[i + 3], b = params[i + 4];
                    pen_.fg_color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
                    i += 4;
                }
            }
        } else if (p == 39) {
            pen_.fg_color = 0xFFFFFF;
        } else if (p >= 40 && p <= 47) {
            pen_.bg_color = kColorTable[p - 40];
        } else if (p == 48) {
            // Extended background
            if (i + 1 < params.size()) {
                int sub = params[i + 1];
                if (sub == 5 && i + 2 < params.size()) {
                    int idx = params[i + 2];
                    if (idx >= 0 && idx < 16)
                        pen_.bg_color = kColorTable[idx];
                    else if (idx >= 16 && idx < 232) {
                        int c = idx - 16;
                        int r = c / 36, g = (c / 6) % 6, b = c % 6;
                        pen_.bg_color = (kCubeValues[r] << 16) | (kCubeValues[g] << 8) | kCubeValues[b];
                    } else if (idx >= 232 && idx < 256) {
                        int gray = 8 + (idx - 232) * 10;
                        pen_.bg_color = (gray << 16) | (gray << 8) | gray;
                    }
                    i += 2;
                } else if (sub == 2 && i + 4 < params.size()) {
                    int r = params[i + 2], g = params[i + 3], b = params[i + 4];
                    pen_.bg_color = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
                    i += 4;
                }
            }
        } else if (p == 49) {
            pen_.bg_color = 0x000000;
        } else if (p >= 90 && p <= 97) {
            pen_.fg_color = kColorTable[p - 90 + 8];
        } else if (p >= 100 && p <= 107) {
            pen_.bg_color = kColorTable[p - 100 + 8];
        }
    }
}

// --- Scroll Region ---
void Screen::handleScrollRegion(const std::vector<int>& params) {
    int top = paramOr(params, 0, 1) - 1;
    int bottom = paramOr(params, 1, rows_) - 1;
    top = std::clamp(top, 0, rows_ - 1);
    bottom = std::clamp(bottom, 0, rows_ - 1);
    if (top < bottom) {
        scroll_top_ = top;
        scroll_bottom_ = bottom;
    }
    cursor_.row = 0;
    cursor_.col = 0;
}

// --- Mode set/reset ---
void Screen::handleMode(char32_t final_char,
                        const std::vector<int>& params,
                        const std::string& intermediates) {
    bool set = (final_char == 'h');
    bool is_private = (intermediates.find('?') != std::string::npos);

    for (int p : params) {
        if (!is_private) continue;

        switch (p) {
        case 1:    // DECCKM - Application cursor keys
            app_cursor_keys_ = set;
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
        default:
            break;
        }
    }
}

// --- Insert/Delete Lines ---
void Screen::handleInsertDeleteLines(char32_t final_char,
                                     const std::vector<int>& params) {
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
                                     const std::vector<int>& params) {
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
}

// --- Scroll Up/Down ---
void Screen::handleScrollUpDown(char32_t final_char,
                                const std::vector<int>& params) {
    int n = paramOr(params, 0, 1);
    if (final_char == 'S') { // SU
        scrollUp(scroll_top_, scroll_bottom_, n);
    } else { // 'T' - SD
        scrollDown(scroll_top_, scroll_bottom_, n);
    }
}

// --- Erase Characters ---
void Screen::handleEraseChars(const std::vector<int>& params) {
    int n = paramOr(params, 0, 1);
    n = std::min(n, cols_ - cursor_.col);
    for (int i = 0; i < n; ++i) {
        eraseCell(mutableCellAt(cursor_.row, cursor_.col + i));
    }
}

// --- Absolute Position ---
void Screen::handleAbsolutePosition(char32_t final_char,
                                    const std::vector<int>& params) {
    int val = paramOr(params, 0, 1) - 1;
    if (final_char == 'd') { // VPA - cursor to absolute row
        cursor_.row = std::clamp(val, 0, rows_ - 1);
    } else { // 'G' - CHA - cursor to absolute column
        cursor_.col = std::clamp(val, 0, cols_ - 1);
    }
}

} // namespace termcore
