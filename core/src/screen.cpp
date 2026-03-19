#include "termcore/screen.h"
#include "termcore/config.h"
#include "screen_colors.h"
#include "termcore/font/unicode_width.h"
#include <algorithm>
#include <cassert>

namespace termcore {

Screen::Screen(int rows, int cols)
    : rows_(rows), cols_(cols), scroll_bottom_(rows - 1)
{
    grid_.resize(rows_, makeRow());
    initTabStops();
}

void Screen::initTabStops() {
    tab_stops_.assign(cols_, false);
    for (int i = 0; i < cols_; i += 8)
        tab_stops_[i] = true;
}

Screen::Row Screen::makeRow() const {
    return Row(cols_);
}

const TermCell& Screen::cellAt(int row, int col) const {
    static const TermCell empty{};
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_)
        return empty;

    if (viewport_offset_ > 0) {
        // When scrolled up, the top `viewport_offset_` rows come from scrollback
        // and the remaining rows come from the grid (shifted)
        int scrollback_rows_visible = std::min(viewport_offset_, rows_);
        if (row < scrollback_rows_visible) {
            // This row comes from scrollback
            int sb_size = static_cast<int>(scrollback_.size());
            int sb_idx = sb_size - viewport_offset_ + row;
            if (sb_idx < 0 || sb_idx >= sb_size)
                return empty;
            const auto& sb_row = scrollback_[sb_idx];
            if (col < static_cast<int>(sb_row.size()))
                return sb_row[col];
            return empty;
        } else {
            // This row comes from the grid
            int grid_row = row - scrollback_rows_visible;
            if (grid_row >= 0 && grid_row < rows_)
                return grid_[grid_row][col];
            return empty;
        }
    }

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
        // If scrolling the whole screen, save to scrollback (not on alt screen)
        if (!alt_screen_active_ && top == scroll_top_ && bottom == scroll_bottom_ && top == 0) {
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
    last_printed_ = codepoint;

    int char_width = codepoint_width(codepoint);
    if (char_width < 1) char_width = 1;  // treat zero-width as single-width for grid

    if (wrap_pending_) {
        wrap_pending_ = false;
        cursor_.col = 0;
        if (cursor_.row == scroll_bottom_) {
            scrollUp(scroll_top_, scroll_bottom_);
        } else if (cursor_.row < rows_ - 1) {
            cursor_.row++;
        }
    }

    // For a wide character that would start at the last column, force wrap first
    if (char_width == 2 && cursor_.col == cols_ - 1) {
        if (autowrap_) {
            cursor_.col = 0;
            if (cursor_.row == scroll_bottom_) {
                scrollUp(scroll_top_, scroll_bottom_);
            } else if (cursor_.row < rows_ - 1) {
                cursor_.row++;
            }
        }
    }

    TermCell& cell = mutableCellAt(cursor_.row, cursor_.col);
    cell.codepoint = codepoint;
    cell.fg_color = pen_.fg_color;
    cell.bg_color = pen_.bg_color;
    cell.attributes = pen_.attributes;
    cell.width = static_cast<uint8_t>(char_width);

    // Write continuation cell for wide characters
    if (char_width == 2 && cursor_.col + 1 < cols_) {
        TermCell& cont = mutableCellAt(cursor_.row, cursor_.col + 1);
        cont.codepoint = 0;  // continuation marker: codepoint=0
        cont.fg_color = pen_.fg_color;
        cont.bg_color = pen_.bg_color;
        cont.attributes = pen_.attributes;
        cont.width = 0;  // continuation cell width=0
    }

    // Advance cursor by the character's display width
    int new_col = cursor_.col + char_width;
    if (new_col < cols_) {
        cursor_.col = new_col;
    } else if (autowrap_) {
        wrap_pending_ = true;
    } else {
        cursor_.col = cols_ - 1;
    }
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
        for (int c = cursor_.col + 1; c < cols_; ++c) {
            if (tab_stops_[c]) { cursor_.col = c; break; }
            if (c == cols_ - 1) { cursor_.col = c; break; }
        }
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
void Screen::onOscDispatch(int osc_number,
                           const std::string& osc_string) {
    switch (osc_number) {
    case 0:  // Set icon name and window title
        title_ = osc_string;
        icon_name_ = osc_string;
        break;
    case 1:  // Set icon name
        icon_name_ = osc_string;
        break;
    case 2:  // Set window title
        title_ = osc_string;
        break;
    case 7:  // Set working directory (file:// URL)
        handleOscWorkingDirectory(osc_string);
        break;
    case 8:  // Hyperlink
        handleOscHyperlink(osc_string);
        break;
    case 9:  // Desktop notification (ConEmu style)
        handleOscNotification(9, osc_string);
        break;
    case 52: // Clipboard
        handleOscClipboard(osc_string);
        break;
    case 99: // Kitty notification
        handleOscNotification(99, osc_string);
        break;
    case 133: // Shell integration prompt marker
        handleOscShellIntegration(osc_string);
        break;
    case 777: // Desktop notification (rxvt-unicode style)
        handleOscNotification(777, osc_string);
        break;
    case 4: // OSC 4: Set/query palette color
        handleOscPaletteColor(osc_string);
        break;
    case 10: case 11: case 12: case 13: case 14:
    case 15: case 16: case 17: case 18: case 19:
        handleOscDynamicColor(osc_number, osc_string);
        break;
    case 104: // Reset palette color(s)
    case 110: case 111: case 112: case 113: case 114:
    case 115: case 116: case 117: case 118: case 119:
        handleOscResetColor(osc_number, osc_string);
        break;
    default:
        break;
    }
}

// --- Viewport scrolling ---
void Screen::scrollViewportUp(int lines) {
    if (lines <= 0 || alt_screen_active_) return;
    int max_offset = static_cast<int>(scrollback_.size());
    viewport_offset_ = std::min(viewport_offset_ + lines, max_offset);
}

void Screen::scrollViewportDown(int lines) {
    if (lines <= 0) return;
    viewport_offset_ = std::max(viewport_offset_ - lines, 0);
}

void Screen::scrollViewportToTop() {
    if (alt_screen_active_) return;
    viewport_offset_ = static_cast<int>(scrollback_.size());
}

void Screen::scrollViewportToBottom() {
    viewport_offset_ = 0;
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
    initTabStops();
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

// --- getScrollbackLineText ---
std::string Screen::getScrollbackLineText(int line) const {
    if (line < 0 || static_cast<size_t>(line) >= scrollback_.size())
        return "";
    // line 0 = most recent = back of deque
    size_t idx = scrollback_.size() - 1 - static_cast<size_t>(line);
    std::string result;
    for (const auto& cell : scrollback_[idx]) {
        char32_t cp = cell.codepoint;
        if (cp < 0x80) result += static_cast<char>(cp);
        else if (cp < 0x800) {
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
    if (pos != std::string::npos) result.erase(pos + 1);
    else result.clear();
    return result;
}

// --- Alt screen ---
void Screen::switchToAltScreen(bool save_cursor) {
    if (alt_screen_active_) return;

    // Save primary state
    saved_primary_.grid = std::move(grid_);
    saved_primary_.cursor = cursor_;
    saved_primary_.pen = pen_;
    saved_primary_.scroll_top = scroll_top_;
    saved_primary_.scroll_bottom = scroll_bottom_;
    saved_primary_.autowrap = autowrap_;
    saved_primary_.wrap_pending = wrap_pending_;

    // Create fresh alt screen
    grid_.clear();
    grid_.resize(rows_, makeRow());
    if (save_cursor) {
        saved_cursor_ = cursor_;
        saved_pen_ = pen_;
    }
    cursor_ = CursorState{};
    scroll_top_ = 0;
    scroll_bottom_ = rows_ - 1;
    wrap_pending_ = false;
    alt_screen_active_ = true;
}

void Screen::switchToPrimaryScreen(bool restore_cursor) {
    if (!alt_screen_active_) return;

    // Restore primary state
    grid_ = std::move(saved_primary_.grid);
    cursor_ = saved_primary_.cursor;
    pen_ = saved_primary_.pen;
    scroll_top_ = saved_primary_.scroll_top;
    scroll_bottom_ = saved_primary_.scroll_bottom;
    autowrap_ = saved_primary_.autowrap;
    wrap_pending_ = saved_primary_.wrap_pending;

    if (restore_cursor) {
        cursor_ = saved_cursor_;
        pen_ = saved_pen_;
    }
    alt_screen_active_ = false;
    clampCursor();
}

void Screen::clearScreen() {
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c)
            eraseCell(mutableCellAt(r, c));
}

void Screen::initDynamicColors(const Config& cfg) {
    dynamic_colors_.initFromConfig(cfg);
}

} // namespace termcore
