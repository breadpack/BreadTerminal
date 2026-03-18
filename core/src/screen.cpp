#include "termcore/screen.h"
#include "screen_colors.h"
#include <algorithm>
#include <cassert>

namespace termcore {

Screen::Screen(int rows, int cols)
    : rows_(rows), cols_(cols), scroll_bottom_(rows - 1)
{
    grid_.resize(rows_, makeRow());
}

Screen::Row Screen::makeRow() const {
    return Row(cols_);
}

const TermCell& Screen::cellAt(int row, int col) const {
    static const TermCell empty{};
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_)
        return empty;
    return grid_[row][col];
}

TermCell& Screen::mutableCellAt(int row, int col) {
    assert(row >= 0 && row < rows_ && col >= 0 && col < cols_);
    return grid_[row][col];
}

void Screen::eraseCell(TermCell& cell) const {
    cell.codepoint = ' ';
    cell.fg_color = pen_.fg_color;
    cell.bg_color = pen_.bg_color;
    cell.attributes = 0;
    cell.width = 1;
}

void Screen::clampCursor() {
    cursor_.row = std::clamp(cursor_.row, 0, rows_ - 1);
    cursor_.col = std::clamp(cursor_.col, 0, cols_ - 1);
}

void Screen::scrollUp(int top, int bottom, int count) {
    count = std::min(count, bottom - top + 1);
    for (int i = 0; i < count; ++i) {
        // If scrolling the whole screen, save to scrollback
        if (top == scroll_top_ && bottom == scroll_bottom_ && top == 0) {
            scrollback_.push_back(std::move(grid_[top]));
            if (scrollback_.size() > max_scrollback_) {
                scrollback_.pop_front();
            }
        }
        // Shift lines up
        for (int r = top; r < bottom; ++r) {
            grid_[r] = std::move(grid_[r + 1]);
        }
        grid_[bottom] = makeRow();
    }
}

void Screen::scrollDown(int top, int bottom, int count) {
    count = std::min(count, bottom - top + 1);
    for (int i = 0; i < count; ++i) {
        for (int r = bottom; r > top; --r) {
            grid_[r] = std::move(grid_[r - 1]);
        }
        grid_[top] = makeRow();
    }
}

// --- onPrint ---
void Screen::onPrint(char32_t codepoint) {
    if (wrap_pending_) {
        wrap_pending_ = false;
        cursor_.col = 0;
        if (cursor_.row == scroll_bottom_) {
            scrollUp(scroll_top_, scroll_bottom_);
        } else if (cursor_.row < rows_ - 1) {
            cursor_.row++;
        }
    }

    TermCell& cell = mutableCellAt(cursor_.row, cursor_.col);
    cell.codepoint = codepoint;
    cell.fg_color = pen_.fg_color;
    cell.bg_color = pen_.bg_color;
    cell.attributes = pen_.attributes;
    cell.width = 1;

    advanceCursorAfterPrint();
}

void Screen::advanceCursorAfterPrint() {
    if (cursor_.col < cols_ - 1) {
        cursor_.col++;
    } else if (autowrap_) {
        wrap_pending_ = true;
    }
}

// --- onExecute ---
void Screen::onExecute(uint8_t byte) {
    switch (byte) {
    case 0x07: // BEL - ignore
        break;
    case 0x08: // BS
        if (cursor_.col > 0) {
            cursor_.col--;
            wrap_pending_ = false;
        }
        break;
    case 0x09: { // HT (tab)
        int next = ((cursor_.col / 8) + 1) * 8;
        cursor_.col = std::min(next, cols_ - 1);
        wrap_pending_ = false;
        break;
    }
    case 0x0A: // LF
    case 0x0B: // VT
    case 0x0C: // FF
        wrap_pending_ = false;
        if (cursor_.row == scroll_bottom_) {
            scrollUp(scroll_top_, scroll_bottom_);
        } else if (cursor_.row < rows_ - 1) {
            cursor_.row++;
        }
        break;
    case 0x0D: // CR
        cursor_.col = 0;
        wrap_pending_ = false;
        break;
    default:
        break;
    }
}

// --- onEscDispatch ---
void Screen::onEscDispatch(char32_t final_char,
                           const std::string& intermediates) {
    if (!intermediates.empty()) return;

    switch (final_char) {
    case 'D': // IND - index
        if (cursor_.row == scroll_bottom_) {
            scrollUp(scroll_top_, scroll_bottom_);
        } else if (cursor_.row < rows_ - 1) {
            cursor_.row++;
        }
        break;
    case 'M': // RI - reverse index
        if (cursor_.row == scroll_top_) {
            scrollDown(scroll_top_, scroll_bottom_);
        } else if (cursor_.row > 0) {
            cursor_.row--;
        }
        break;
    case 'E': // NEL - next line
        cursor_.col = 0;
        if (cursor_.row == scroll_bottom_) {
            scrollUp(scroll_top_, scroll_bottom_);
        } else if (cursor_.row < rows_ - 1) {
            cursor_.row++;
        }
        break;
    case '7': // DECSC - save cursor
        saved_cursor_ = cursor_;
        saved_pen_ = pen_;
        break;
    case '8': // DECRC - restore cursor
        cursor_ = saved_cursor_;
        pen_ = saved_pen_;
        clampCursor();
        break;
    default:
        break;
    }
}

// --- onOscDispatch ---
void Screen::onOscDispatch(int /*osc_number*/,
                           const std::string& /*osc_string*/) {
    // TODO: handle title changes, etc.
}

// --- resize ---
void Screen::resize(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return;

    // Resize columns for existing rows
    for (auto& row : grid_) {
        row.resize(cols);
    }

    // Adjust row count
    if (rows > rows_) {
        for (int i = rows_; i < rows; ++i) {
            grid_.push_back(Row(cols));
        }
    } else if (rows < rows_) {
        grid_.resize(rows);
    }

    rows_ = rows;
    cols_ = cols;
    scroll_bottom_ = rows_ - 1;
    scroll_top_ = 0;
    clampCursor();
    wrap_pending_ = false;
}

// --- getLineText ---
std::string Screen::getLineText(int row) const {
    if (row < 0 || row >= rows_) return "";
    std::string result;
    for (int c = 0; c < cols_; ++c) {
        char32_t cp = grid_[row][c].codepoint;
        // Encode as UTF-8
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    // Trim trailing spaces
    auto pos = result.find_last_not_of(' ');
    if (pos != std::string::npos) {
        result.erase(pos + 1);
    } else {
        result.clear();
    }
    return result;
}

} // namespace termcore
