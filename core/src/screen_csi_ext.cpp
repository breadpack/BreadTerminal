#include "termcore/screen.h"
#include <algorithm>

namespace termcore {

static int paramOrDefault(const std::vector<int>& params, size_t idx, int def) {
    if (idx < params.size() && params[idx] > 0)
        return params[idx];
    return def;
}

// --- DSR - Device Status Report ---
void Screen::handleDeviceStatusReport(const std::vector<int>& params,
                                      const std::string& /*intermediates*/) {
    if (!response_callback_) return;
    int mode = paramOrDefault(params, 0, 0);

    switch (mode) {
    case 5: // Device status → OK
        response_callback_("\033[0n");
        break;
    case 6: { // Cursor position report (1-based)
        std::string resp = "\033[" + std::to_string(cursor_.row + 1)
                         + ";" + std::to_string(cursor_.col + 1) + "R";
        response_callback_(resp);
        break;
    }
    default:
        break;
    }
}

// --- DA - Device Attributes ---
void Screen::handleDeviceAttributes(const std::vector<int>& params,
                                    const std::string& intermediates) {
    if (!response_callback_) return;
    bool secondary = (intermediates.find('>') != std::string::npos);
    int p = paramOrDefault(params, 0, 0);

    if (secondary) {
        // Secondary DA: CSI > c or CSI > 0 c
        if (p == 0) {
            response_callback_("\033[>1;0;0c");
        }
    } else {
        // Primary DA: CSI c or CSI 0 c
        if (p == 0) {
            response_callback_("\033[?1;2c");
        }
    }
}

// --- DECSCUSR - Set Cursor Style ---
void Screen::handleCursorStyle(const std::vector<int>& params) {
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
void Screen::handleRepeatChar(const std::vector<int>& params) {
    if (last_printed_ == 0) return;
    int n = paramOrDefault(params, 0, 1);
    for (int i = 0; i < n; ++i) {
        onPrint(last_printed_);
    }
}

// --- CNL/CPL - Cursor Next/Previous Line ---
void Screen::handleCursorNextPrevLine(char32_t final_char,
                                      const std::vector<int>& params) {
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
                               const std::vector<int>& params) {
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
void Screen::handleTabClear(const std::vector<int>& params) {
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

} // namespace termcore
