#include "termcore/screen.h"
#include <algorithm>

namespace termcore {

static int paramOrDefault(const std::vector<VtParam>& params, size_t idx, int def) {
    if (idx < params.size() && params[idx].value > 0)
        return params[idx].value;
    return def;
}

// --- DSR - Device Status Report ---
void Screen::handleDeviceStatusReport(const std::vector<VtParam>& params,
                                      const std::string& /*intermediates*/) {
    if (!response_callback_) return;
    int mode = paramOrDefault(params, 0, 0);

    switch (mode) {
    case 5: // Device status -> OK
        response_callback_("\033[0n");
        break;
    case 6: { // Cursor position report (1-based)
        int report_row = cursor_.row + 1;
        if (origin_mode_) {
            report_row = cursor_.row - scroll_top_ + 1;
        }
        std::string resp = "\033[" + std::to_string(report_row)
                         + ";" + std::to_string(cursor_.col + 1) + "R";
        response_callback_(resp);
        break;
    }
    default:
        break;
    }
}

// --- DA - Device Attributes ---
void Screen::handleDeviceAttributes(const std::vector<VtParam>& params,
                                    const std::string& intermediates) {
    if (!response_callback_) return;
    bool secondary = (intermediates.find('>') != std::string::npos);
    int p = paramOrDefault(params, 0, 0);

    if (secondary) {
        // Secondary DA: CSI > c or CSI > 0 c
        if (p == 0) {
            response_callback_("\033[>65;1;0c");
        }
    } else {
        // Primary DA: CSI c or CSI 0 c
        if (p == 0) {
            response_callback_("\033[?62;22c");
        }
    }
}

// --- DECSCUSR - Set Cursor Style ---
void Screen::handleCursorStyle(const std::vector<VtParam>& params) {
    int style = paramOrDefault(params, 0, 0);

    switch (style) {
    case 0: // Default (blinking block)
        cursor_.shape = CursorShape::Block;
        cursor_.blink = true;
        break;
    case 1: // Blinking block
        cursor_.shape = CursorShape::Block;
        cursor_.blink = true;
        break;
    case 2: // Steady block
        cursor_.shape = CursorShape::Block;
        cursor_.blink = false;
        break;
    case 3: // Blinking underline
        cursor_.shape = CursorShape::Underline;
        cursor_.blink = true;
        break;
    case 4: // Steady underline
        cursor_.shape = CursorShape::Underline;
        cursor_.blink = false;
        break;
    case 5: // Blinking bar
        cursor_.shape = CursorShape::Bar;
        cursor_.blink = true;
        break;
    case 6: // Steady bar
        cursor_.shape = CursorShape::Bar;
        cursor_.blink = false;
        break;
    default:
        break;
    }
}

// --- REP - Repeat preceding character ---
void Screen::handleRepeatChar(const std::vector<VtParam>& params) {
    if (last_printed_ == 0) return;
    int n = paramOrDefault(params, 0, 1);
    for (int i = 0; i < n; ++i) {
        onPrint(last_printed_);
    }
}

// --- CNL/CPL - Cursor Next/Previous Line ---
void Screen::handleCursorNextPrevLine(char32_t final_char,
                                      const std::vector<VtParam>& params) {
    int n = paramOrDefault(params, 0, 1);

    if (final_char == 'E') { // CNL - cursor next line
        cursor_.row = std::min(rows_ - 1, cursor_.row + n);
    } else { // 'F' - CPL - cursor previous line
        cursor_.row = std::max(0, cursor_.row - n);
    }
    cursor_.col = 0;
}

// --- CHT/CBT - Tab Movement ---
void Screen::handleTabMovement(char32_t final_char,
                               const std::vector<VtParam>& params) {
    int n = paramOrDefault(params, 0, 1);

    if (final_char == 'I') { // CHT - forward tab
        for (int i = 0; i < n; ++i) {
            bool found = false;
            for (int c = cursor_.col + 1; c < cols_; ++c) {
                if (tab_stops_[c]) {
                    cursor_.col = c;
                    found = true;
                    break;
                }
            }
            if (!found) {
                cursor_.col = cols_ - 1;
                break;
            }
        }
    } else { // 'Z' - CBT - backward tab
        for (int i = 0; i < n; ++i) {
            bool found = false;
            for (int c = cursor_.col - 1; c >= 0; --c) {
                if (tab_stops_[c]) {
                    cursor_.col = c;
                    found = true;
                    break;
                }
            }
            if (!found) {
                cursor_.col = 0;
                break;
            }
        }
    }
}

// --- TBC - Tab Clear ---
void Screen::handleTabClear(const std::vector<VtParam>& params) {
    int mode = paramOrDefault(params, 0, 0);

    switch (mode) {
    case 0: // Clear tab stop at current column
        if (cursor_.col >= 0 && cursor_.col < cols_) {
            tab_stops_[cursor_.col] = false;
        }
        break;
    case 3: // Clear all tab stops
        std::fill(tab_stops_.begin(), tab_stops_.end(), false);
        break;
    default:
        break;
    }
}

// --- DECRQM/DECRPM - Request Mode / Report Mode ---
void Screen::handleModeQuery(const std::vector<VtParam>& params,
                             const std::string& intermediates) {
    if (!response_callback_ || params.empty()) return;

    int mode = params[0].value;
    bool is_private = (intermediates.find('?') != std::string::npos);

    if (is_private) {
        // Private mode query: CSI ? Ps $ p  -> CSI ? Ps ; Pm $ y
        // Pm: 0 = not recognized, 1 = set, 2 = reset
        int pm = 0; // not recognized by default

        switch (mode) {
        case 1:    // DECCKM
            pm = app_cursor_keys_ ? 1 : 2;
            break;
        case 6:    // DECOM - Origin mode
            pm = origin_mode_ ? 1 : 2;
            break;
        case 7:    // DECAWM
            pm = autowrap_ ? 1 : 2;
            break;
        case 12:   // Cursor blink
            pm = cursor_.blink ? 1 : 2;
            break;
        case 25:   // DECTCEM
            pm = cursor_.visible ? 1 : 2;
            break;
        case 47:   // Alt screen (no save/restore)
            pm = alt_screen_active_ ? 1 : 2;
            break;
        case 1000: // Mouse X10
            pm = (mouse_mode_ == MouseMode::X10) ? 1 : 2;
            break;
        case 1002: // Mouse button event
            pm = (mouse_mode_ == MouseMode::ButtonEvent) ? 1 : 2;
            break;
        case 1003: // Mouse any event
            pm = (mouse_mode_ == MouseMode::AnyEvent) ? 1 : 2;
            break;
        case 1004: // Focus events
            pm = focus_events_ ? 1 : 2;
            break;
        case 1006: // SGR mouse encoding
            pm = (mouse_encoding_ == MouseEncoding::SGR) ? 1 : 2;
            break;
        case 1047: // Alt screen (clear on enter)
            pm = alt_screen_active_ ? 1 : 2;
            break;
        case 1049: // Alt screen + save/restore cursor
            pm = alt_screen_active_ ? 1 : 2;
            break;
        case 2004: // Bracketed paste
            pm = bracketed_paste_ ? 1 : 2;
            break;
        case 2026: // Synchronized output
            pm = sync_update_ ? 1 : 2;
            break;
        default:
            pm = 0; // not recognized
            break;
        }

        std::string resp = "\033[?" + std::to_string(mode)
                         + ";" + std::to_string(pm) + "$y";
        response_callback_(resp);
    } else {
        // Standard (ANSI) mode query: CSI Ps $ p -> CSI Ps ; Pm $ y
        int pm = 0;
        switch (mode) {
        case 4: // IRM
            pm = insert_mode_ ? 1 : 2;
            break;
        default:
            pm = 0;
            break;
        }
        std::string resp = "\033[" + std::to_string(mode) + ";" + std::to_string(pm) + "$y";
        response_callback_(resp);
    }
}

} // namespace termcore
