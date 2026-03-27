#include "termcore/screen.h"
#include "termcore/config.h"
#include "screen_colors.h"
#include "termcore/font/unicode_width.h"
#include <algorithm>
#include <cassert>

namespace termcore {

Screen::Screen(int rows, int cols)
    : rows_(rows), cols_(cols), scrollback_ring_(cols), scroll_bottom_(rows - 1)
{
    grid_.resize(rows_, makeRow());
    row_dirty_.assign(rows_, true);
    screen_dirty_ = true;
    initTabStops();
}

void Screen::initTabStops() {
    tab_stops_.assign(cols_, false);
    for (int i = 0; i < cols_; i += 8)
        tab_stops_[i] = true;
}

Screen::Row Screen::makeRow() const {
    Row row;
    row.cells.resize(cols_);
    row.occ = 0;
    return row;
}

const TermCell& Screen::cellAt(int row, int col) const {
    static const TermCell empty{};
    int gridSize = static_cast<int>(grid_.size());
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_)
        return empty;

    if (viewport_offset_ > 0) {
        // When scrolled up, the top `viewport_offset_` rows come from scrollback
        // and the remaining rows come from the grid (shifted)
        int scrollback_rows_visible = std::min(viewport_offset_, rows_);
        if (row < scrollback_rows_visible) {
            // This row comes from scrollback
            int sb_size = static_cast<int>(scrollback_ring_.size());
            int sb_idx = sb_size - viewport_offset_ + row;
            if (sb_idx < 0 || sb_idx >= sb_size)
                return empty;
            // Cache reconstructed cell in thread-local static to return by reference
            static thread_local TermCell cached_cell;
            cached_cell = scrollback_ring_.cellAt(sb_idx, col);
            return cached_cell;
        } else {
            // This row comes from the grid
            int grid_row = row - scrollback_rows_visible;
            if (grid_row >= 0 && grid_row < gridSize)
                return grid_[grid_row][col];
            return empty;
        }
    }

    if (row >= gridSize) return empty;
    return grid_[row][col];
}

TermCell& Screen::mutableCellAt(int row, int col) {
    assert(row >= 0 && row < rows_ && col >= 0 && col < cols_);
    // Defensive: clamp to actual grid bounds to prevent deque out-of-range
    int gridRows = static_cast<int>(grid_.size());
    if (row >= gridRows) {
        // Ensure grid has enough rows (recover from size mismatch)
        while (static_cast<int>(grid_.size()) <= row) {
            grid_.push_back(makeRow());
        }
    }
    return grid_[row][col];
}

// --- Dirty tracking ---
bool Screen::isRowDirty(int row) const {
    if (row < 0 || row >= rows_) return false;
    return row_dirty_[row];
}

void Screen::clearDirty() {
    std::fill(row_dirty_.begin(), row_dirty_.end(), false);
    screen_dirty_ = false;
}

void Screen::markRowDirty(int row) {
    if (row >= 0 && row < rows_) {
        row_dirty_[row] = true;
        screen_dirty_ = true;
    }
}

void Screen::markAllDirty() {
    std::fill(row_dirty_.begin(), row_dirty_.end(), true);
    screen_dirty_ = true;
}

void Screen::eraseCell(TermCell& cell) const {
    cell.codepoint = ' ';
    cell.fg_color = pen_.fg_color;
    cell.bg_color = pen_.bg_color;
    cell.attributes = 0;
    cell.width = 1;
    cell.underline_style = UnderlineNone;
    cell.underline_color = kColorDefault;
    cell.extra_count = 0;
}

void Screen::clampCursor() {
    cursor_.row = std::clamp(cursor_.row, 0, rows_ - 1);
    cursor_.col = std::clamp(cursor_.col, 0, cols_ - 1);
}

void Screen::scrollUp(int top, int bottom, int count) {
    count = std::min(count, bottom - top + 1);

    // Mark affected rows dirty
    for (int r = top; r <= bottom; ++r)
        markRowDirty(r);

    // Fast path: scrolling entire grid from row 0 — O(1) per line via deque
    if (top == 0 && bottom == rows_ - 1) {
        for (int i = 0; i < count; ++i) {
            if (top == scroll_top_ && bottom == scroll_bottom_) {
                scrollback_ring_.pushRow(grid_.front().cells);
            }
            grid_.pop_front();
            grid_.push_back(makeRow());
        }
        return;
    }

    // Slow path: partial scroll region — O(region) shift
    for (int i = 0; i < count; ++i) {
        if (top == scroll_top_ && bottom == scroll_bottom_ && top == 0) {
            scrollback_ring_.pushRow(grid_[top].cells);
        }
        for (int r = top; r < bottom; ++r) {
            grid_[r] = std::move(grid_[r + 1]);
        }
        grid_[bottom] = makeRow();
    }
}

void Screen::scrollDown(int top, int bottom, int count) {
    count = std::min(count, bottom - top + 1);

    // Mark affected rows dirty
    for (int r = top; r <= bottom; ++r)
        markRowDirty(r);

    // Fast path: scrolling entire grid from row 0 — O(1) per line via deque
    if (top == 0 && bottom == rows_ - 1) {
        for (int i = 0; i < count; ++i) {
            grid_.pop_back();
            grid_.push_front(makeRow());
        }
        return;
    }

    // Slow path: partial scroll region
    for (int i = 0; i < count; ++i) {
        for (int r = bottom; r > top; --r) {
            grid_[r] = std::move(grid_[r - 1]);
        }
        grid_[top] = makeRow();
    }
}

// --- Grapheme cluster combining helpers ---

bool Screen::shouldCombineWithPrevious(char32_t codepoint) const {
    int prev_col = getPreviousCellCol();
    if (prev_col < 0) return false;
    if (grapheme_row_ < 0 || grapheme_row_ >= rows_) return false;

    const TermCell& prev = grid_[grapheme_row_][prev_col];
    if (prev.codepoint == ' ' && prev.extra_count == 0) return false;

    char32_t last_cp = prev.extra_count > 0
        ? prev.extra[prev.extra_count - 1]
        : prev.codepoint;

    GBP prev_prop = graphemeBreakProperty(last_cp);
    GBP cur_prop = graphemeBreakProperty(codepoint);

    // Combining marks (Extend) and SpacingMark always attach (GB9, GB9a)
    if (cur_prop == GBP::Extend || cur_prop == GBP::SpacingMark) return true;

    // ZWJ attaches to previous (GB9)
    if (cur_prop == GBP::ZWJ) return true;

    // After ZWJ, Extended_Pictographic continues the cluster (GB11)
    if (prev_prop == GBP::ZWJ && cur_prop == GBP::Extended_Pictographic) {
        GBP base_prop = graphemeBreakProperty(prev.codepoint);
        if (base_prop == GBP::Extended_Pictographic) return true;
        for (uint8_t i = 0; i < prev.extra_count; ++i) {
            if (graphemeBreakProperty(prev.extra[i]) == GBP::Extended_Pictographic)
                return true;
        }
    }

    // Regional indicator pairing (GB12/GB13)
    if (cur_prop == GBP::Regional_Indicator) {
        int ri_count = 0;
        if (graphemeBreakProperty(prev.codepoint) == GBP::Regional_Indicator)
            ri_count++;
        for (uint8_t i = 0; i < prev.extra_count; ++i) {
            if (graphemeBreakProperty(prev.extra[i]) == GBP::Regional_Indicator)
                ri_count++;
        }
        return ri_count == 1;  // combine only to form a pair
    }

    return false;
}

int Screen::getPreviousCellCol() const {
    if (grapheme_col_ <= 0) return -1;
    for (int c = grapheme_col_ - 1; c >= 0; --c) {
        const TermCell& cell = grid_[grapheme_row_][c];
        if (cell.width > 0 || cell.codepoint != 0) return c;
    }
    return -1;
}

int Screen::graphemeClusterWidth(const TermCell& cell) const {
    GBP base_prop = graphemeBreakProperty(cell.codepoint);

    // Regional indicator pairs -> 2
    if (base_prop == GBP::Regional_Indicator && cell.extra_count > 0) return 2;

    // Extended_Pictographic with VS16, ZWJ, or skin tone modifier -> 2
    if (base_prop == GBP::Extended_Pictographic) {
        for (uint8_t i = 0; i < cell.extra_count; ++i) {
            char32_t cp = cell.extra[i];
            if (cp == 0xFE0F || cp == 0x200D ||
                (cp >= 0x1F3FB && cp <= 0x1F3FF))
                return 2;
        }
    }

    // ZWJ sequences containing Extended_Pictographic -> 2
    bool has_ext_pic = (base_prop == GBP::Extended_Pictographic);
    bool has_zwj = false;
    for (uint8_t i = 0; i < cell.extra_count; ++i) {
        GBP p = graphemeBreakProperty(cell.extra[i]);
        if (p == GBP::Extended_Pictographic) has_ext_pic = true;
        if (p == GBP::ZWJ) has_zwj = true;
    }
    if (has_ext_pic && has_zwj) return 2;

    int w = codepoint_width(cell.codepoint);
    return w > 0 ? w : 1;
}

// --- onPrint ---
void Screen::onPrint(char32_t codepoint) {
    last_printed_ = codepoint;

    // Check if this codepoint should combine with the previous cell
    if (shouldCombineWithPrevious(codepoint)) {
        int prev_col = getPreviousCellCol();
        if (prev_col >= 0) {
            TermCell& prev = mutableCellAt(grapheme_row_, prev_col);
            prev.appendCodepoint(codepoint);

            int new_width = graphemeClusterWidth(prev);
            int old_width = prev.width;
            if (new_width != old_width) {
                prev.width = static_cast<uint8_t>(new_width);
                if (new_width == 2 && old_width == 1 && prev_col + 1 < cols_) {
                    TermCell& cont = mutableCellAt(grapheme_row_, prev_col + 1);
                    cont.codepoint = 0;
                    cont.fg_color = prev.fg_color;
                    cont.bg_color = prev.bg_color;
                    cont.attributes = prev.attributes;
                    cont.width = 0;
                    cont.extra_count = 0;
                    cont.underline_style = prev.underline_style;
                    cont.underline_color = prev.underline_color;
                    grid_[grapheme_row_].markOccupied(prev_col + 1);
                }
            }
            markRowDirty(grapheme_row_);
            return;
        }
    }

    int char_width = codepoint_width(codepoint);
    if (char_width < 1) char_width = 1;

    markRowDirty(cursor_.row);

    if (wrap_pending_) {
        wrap_pending_ = false;
        cursor_.col = 0;
        if (cursor_.row == scroll_bottom_) {
            scrollUp(scroll_top_, scroll_bottom_);
        } else if (cursor_.row < rows_ - 1) {
            cursor_.row++;
        }
        markRowDirty(cursor_.row);
    }

    if (char_width == 2 && cursor_.col == cols_ - 1) {
        if (autowrap_) {
            cursor_.col = 0;
            if (cursor_.row == scroll_bottom_) {
                scrollUp(scroll_top_, scroll_bottom_);
            } else if (cursor_.row < rows_ - 1) {
                cursor_.row++;
            }
            markRowDirty(cursor_.row);
        }
    }

    if (insert_mode_) {
        auto& row = grid_[cursor_.row];
        int shift = char_width;
        shift = std::min(shift, cols_ - cursor_.col);
        for (int s = 0; s < shift; ++s) {
            row.insert(row.begin() + cursor_.col, TermCell{});
        }
        row.resize(cols_);
        row.occ = std::min(row.occ + shift, cols_);
    }

    TermCell& cell = mutableCellAt(cursor_.row, cursor_.col);
    cell.codepoint = codepoint;
    cell.fg_color = pen_.fg_color;
    cell.bg_color = pen_.bg_color;
    cell.attributes = pen_.attributes;
    cell.width = static_cast<uint8_t>(char_width);
    cell.underline_style = pen_.underline_style;
    cell.underline_color = pen_.underline_color;
    cell.extra_count = 0;

    // Track occupancy for fast row clearing
    grid_[cursor_.row].markOccupied(cursor_.col + char_width - 1);

    grapheme_row_ = cursor_.row;
    grapheme_col_ = cursor_.col + char_width;

    if (char_width == 2 && cursor_.col + 1 < cols_) {
        TermCell& cont = mutableCellAt(cursor_.row, cursor_.col + 1);
        cont.codepoint = 0;
        cont.fg_color = pen_.fg_color;
        cont.bg_color = pen_.bg_color;
        cont.attributes = pen_.attributes;
        cont.width = 0;
        cont.underline_style = pen_.underline_style;
        cont.underline_color = pen_.underline_color;
        cont.extra_count = 0;
    }

    int new_col = cursor_.col + char_width;
    if (new_col < cols_) {
        cursor_.col = new_col;
    } else if (autowrap_) {
        wrap_pending_ = true;
    } else {
        cursor_.col = cols_ - 1;
    }
}

void Screen::onPrintAscii(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char32_t cp = static_cast<char32_t>(data[i]);
        last_printed_ = cp;

        markRowDirty(cursor_.row);

        if (wrap_pending_) {
            wrap_pending_ = false;
            cursor_.col = 0;
            if (cursor_.row == scroll_bottom_) {
                scrollUp(scroll_top_, scroll_bottom_);
            } else if (cursor_.row < rows_ - 1) {
                cursor_.row++;
            }
            markRowDirty(cursor_.row);
        }

        if (insert_mode_) {
            auto& row = grid_[cursor_.row];
            row.insert(row.begin() + cursor_.col, TermCell{});
            row.resize(cols_);
            row.occ = std::min(row.occ + 1, cols_);
        }

        TermCell& cell = mutableCellAt(cursor_.row, cursor_.col);
        cell.codepoint = cp;
        cell.fg_color = pen_.fg_color;
        cell.bg_color = pen_.bg_color;
        cell.attributes = pen_.attributes;
        cell.width = 1;
        cell.underline_style = pen_.underline_style;
        cell.underline_color = pen_.underline_color;
        cell.extra_count = 0;

        grid_[cursor_.row].markOccupied(cursor_.col);

        grapheme_row_ = cursor_.row;
        grapheme_col_ = cursor_.col + 1;

        if (cursor_.col < cols_ - 1) {
            cursor_.col++;
        } else if (autowrap_) {
            wrap_pending_ = true;
        } else {
            cursor_.col = cols_ - 1;
        }
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
            markRowDirty(cursor_.row);
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
            markRowDirty(cursor_.row);
        }
        break;
    case 'M': // RI - reverse index
        if (cursor_.row == scroll_top_) {
            scrollDown(scroll_top_, scroll_bottom_);
        } else if (cursor_.row > 0) {
            cursor_.row--;
            markRowDirty(cursor_.row);
        }
        break;
    case 'E': // NEL - next line
        cursor_.col = 0;
        if (cursor_.row == scroll_bottom_) {
            scrollUp(scroll_top_, scroll_bottom_);
        } else if (cursor_.row < rows_ - 1) {
            cursor_.row++;
            markRowDirty(cursor_.row);
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
    case 1337: // iTerm2 inline image protocol
        handleOscItermImage(osc_string);
        break;
    case 7770: // BreadTerminal hook event protocol
        handleOscHookEvent(osc_string);
        break;
    default:
        break;
    }
}

// --- Viewport scrolling ---
void Screen::scrollViewportUp(int lines) {
    if (lines <= 0) return;
    int max_offset = static_cast<int>(scrollback_ring_.size());
    viewport_offset_ = std::min(viewport_offset_ + lines, max_offset);
}

void Screen::scrollViewportDown(int lines) {
    if (lines <= 0) return;
    viewport_offset_ = std::max(viewport_offset_ - lines, 0);
}

void Screen::scrollViewportToTop() {
    viewport_offset_ = static_cast<int>(scrollback_ring_.size());
}

void Screen::scrollViewportToBottom() {
    viewport_offset_ = 0;
}

// --- onDcsDispatch ---
void Screen::onDcsDispatch(char32_t final_char,
                           const std::vector<VtParam>& params,
                           const std::string& intermediates,
                           const std::string& data) {
    (void)intermediates;

    // Sixel graphics: DCS P1;P2;P3 q <sixel-data> ST
    if (final_char == 'q') {
        handleSixelImage(params, data);
        return;
    }

    // tmux DCS passthrough: ESC P tmux; <escaped-sequence> ST
    // In the DCS state machine, 't' is the final char that transitions to
    // passthrough, so data starts with "mux;" followed by the inner sequence
    // with doubled ESCs (ESC ESC -> ESC).
    static const std::string kTmuxDataPrefix = "mux;";
    if (final_char == 't' &&
        data.size() > kTmuxDataPrefix.size() &&
        data.compare(0, kTmuxDataPrefix.size(), kTmuxDataPrefix) == 0) {

        if (!parser_feed_callback_) return;

        // Extract the inner sequence after "mux;"
        std::string inner;
        inner.reserve(data.size() - kTmuxDataPrefix.size());
        for (size_t i = kTmuxDataPrefix.size(); i < data.size(); ++i) {
            inner.push_back(data[i]);
            // Un-double ESC: ESC ESC -> ESC (skip the second ESC)
            if (static_cast<uint8_t>(data[i]) == 0x1B &&
                i + 1 < data.size() &&
                static_cast<uint8_t>(data[i + 1]) == 0x1B) {
                ++i;  // skip the doubled ESC
            }
        }

        // Re-feed the unwrapped sequence through the parser
        parser_feed_callback_(inner.data(), inner.size());
    }
}

// --- handleSixelImage ---
void Screen::handleSixelImage(const std::vector<VtParam>& params,
                               const std::string& data) {
    (void)params;

    SixelImage sixel = parseSixel(data);
    if (sixel.empty()) return;

    // Convert SixelImage pixels (uint32_t: R<<24|G<<16|B<<8|A) to uint8_t RGBA array
    std::vector<uint8_t> rgba(sixel.width * sixel.height * 4);
    for (size_t i = 0; i < sixel.pixels.size(); ++i) {
        uint32_t px = sixel.pixels[i];
        rgba[i * 4 + 0] = static_cast<uint8_t>((px >> 24) & 0xFF); // R
        rgba[i * 4 + 1] = static_cast<uint8_t>((px >> 16) & 0xFF); // G
        rgba[i * 4 + 2] = static_cast<uint8_t>((px >> 8) & 0xFF);  // B
        rgba[i * 4 + 3] = static_cast<uint8_t>(px & 0xFF);         // A
    }

    // Create KittyImage from Sixel data
    KittyImage image;
    image.width = sixel.width;
    image.height = sixel.height;
    image.format = 32; // RGBA
    image.data = std::move(rgba);
    image.complete = true;

    uint32_t img_id = kitty_graphics_.addImage(std::move(image));

    // Calculate display cells
    int display_rows = (sixel.height + cell_height_px_ - 1) / cell_height_px_;
    int display_cols = (sixel.width + cell_width_px_ - 1) / cell_width_px_;

    // Create placement at current cursor position
    KittyPlacement placement;
    placement.image_id = img_id;
    placement.col = cursor_.col;
    placement.absolute_row = absoluteRowMonotonic();
    placement.cols = display_cols;
    placement.rows = display_rows;

    kitty_graphics_.addPlacement(placement);

    // Move cursor past the image area (same pattern as iTerm2 inline images)
    cursor_.row += display_rows;
    if (cursor_.row >= rows_) {
        int overflow = cursor_.row - rows_ + 1;
        for (int i = 0; i < overflow; ++i) {
            scrollUp(scroll_top_, scroll_bottom_);
        }
        cursor_.row = rows_ - 1;
    }
    cursor_.col = 0;
    wrap_pending_ = false;

    markAllDirty();
}

// --- onApcDispatch: Kitty graphics protocol ---
void Screen::onApcDispatch(const std::string& data) {
    // Kitty graphics protocol: APC content starts with 'G'
    // Format: G<control>;<payload>  or  G<control>
    if (data.empty() || data[0] != 'G') return;

    // Split into control (key=value pairs) and payload (base64 data)
    std::string control;
    std::string payload;
    auto semicolonPos = data.find(';', 1);
    if (semicolonPos != std::string::npos) {
        control = data.substr(1, semicolonPos - 1);
        payload = data.substr(semicolonPos + 1);
    } else {
        control = data.substr(1);
    }

    // Set cursor position so placements get correct absolute row
    kitty_graphics_.setCursorPosition(cursor_.col, absoluteRowMonotonic());

    std::string response = kitty_graphics_.processCommand(control, payload);

    // Send response back to PTY if needed (e.g. for query commands)
    if (!response.empty() && response_callback_) {
        // Wrap response in APC: ESC _ G<response> ESC backslash
        std::string apc_response = "\033_G" + response + "\033\\";
        response_callback_(apc_response);
    }
}

// --- resize ---
void Screen::resize(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return;

    // Resize columns for existing rows
    for (auto& row : grid_) {
        row.resize(cols);
    }

    // Adjust row count — use actual grid size to avoid mismatch with rows_
    int currentGridRows = static_cast<int>(grid_.size());
    if (rows > currentGridRows) {
        for (int i = currentGridRows; i < rows; ++i) {
            Row row;
            row.cells.resize(cols);
            row.occ = 0;
            grid_.push_back(std::move(row));
        }
    } else if (rows < currentGridRows) {
        grid_.resize(rows);
    }

    rows_ = rows;
    cols_ = cols;
    scroll_bottom_ = rows_ - 1;
    scroll_top_ = 0;
    row_dirty_.assign(rows_, true);
    screen_dirty_ = true;
    initTabStops();
    clampCursor();
    wrap_pending_ = false;

    // Clamp viewport offset to valid range after resize
    if (viewport_offset_ > 0) {
        int max_offset = static_cast<int>(scrollback_ring_.size());
        viewport_offset_ = std::min(viewport_offset_, max_offset);
    }

    // If in alt screen, also resize the saved primary grid so that
    // switching back won't cause a size mismatch with rows_/cols_.
    if (alt_screen_active_) {
        for (auto& row : saved_primary_.grid) {
            row.resize(cols);
        }
        int savedRows = static_cast<int>(saved_primary_.grid.size());
        if (rows > savedRows) {
            for (int i = savedRows; i < rows; ++i) {
                Row row;
                row.cells.resize(cols);
                row.occ = 0;
                saved_primary_.grid.push_back(std::move(row));
            }
        } else if (rows < savedRows) {
            saved_primary_.grid.resize(rows);
        }
    }
}

// --- getLineText ---
std::string Screen::getLineText(int row) const {
    if (row < 0 || row >= rows_) return "";
    std::string result;
    for (int c = 0; c < cols_; ++c) {
        const TermCell& cell = grid_[row][c];
        // Skip continuation cells
        if (cell.codepoint == 0 && cell.width == 0) continue;

        utf8_encode(cell.codepoint, result);
        for (uint8_t i = 0; i < cell.extra_count; ++i) {
            utf8_encode(cell.extra[i], result);
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
    if (line < 0 || static_cast<size_t>(line) >= scrollback_ring_.size())
        return "";
    // line 0 = most recent = last in ring, line N = older
    int idx = static_cast<int>(scrollback_ring_.size()) - 1 - line;
    return scrollback_ring_.rowText(idx);
}

// --- currentInputText ---
std::string Screen::currentInputText() const {
    if (prompt_state_ != PromptState::Input) return "";
    if (input_start_row_ < 0 || input_start_col_ < 0) return "";

    int absRow = static_cast<int>(scrollback_ring_.size()) + cursor_.row;

    std::string result;
    for (int r = input_start_row_; r <= absRow; ++r) {
        int viewRow = r - static_cast<int>(scrollback_ring_.size());
        if (viewRow < 0 || viewRow >= rows_) continue;

        int startCol = (r == input_start_row_) ? input_start_col_ : 0;
        int endCol = (r == absRow) ? cursor_.col : cols_;

        for (int c = startCol; c < endCol && c < cols_; ++c) {
            const TermCell& cell = grid_[viewRow][c];
            if (cell.width == 0) continue;
            if (cell.codepoint == 0) continue;

            char32_t cp = cell.codepoint;
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
        if (r < absRow) result += '\n';
    }
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
    saved_primary_.origin_mode = origin_mode_;

    // Save viewport offset so we can restore when returning to primary screen
    saved_primary_.viewport_offset = viewport_offset_;

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
    viewport_offset_ = 0;
    alt_screen_active_ = true;
    markAllDirty();
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
    origin_mode_ = saved_primary_.origin_mode;
    viewport_offset_ = saved_primary_.viewport_offset;

    if (restore_cursor) {
        cursor_ = saved_cursor_;
        pen_ = saved_pen_;
    }
    alt_screen_active_ = false;
    clampCursor();
    markAllDirty();
}

void Screen::clearScreen() {
    TermCell defaultCell;
    eraseCell(defaultCell);
    for (int r = 0; r < rows_; ++r)
        grid_[r].clear(defaultCell);
    markAllDirty();
}

void Screen::initDynamicColors(const Config& cfg) {
    dynamic_colors_.initFromConfig(cfg);
}

} // namespace termcore
