#include "termcore/vi_copy_mode.h"
#include "termcore/screen.h"
#include <algorithm>

namespace termcore {

ViCopyMode::ViCopyMode(Screen& screen) : screen_(screen) {}

void ViCopyMode::enterCopyMode() {
    active_ = true;
    mode_ = ViMode::Normal;
    cursor_row_ = screen_.cursorRow();
    cursor_col_ = screen_.cursorCol();
    anchor_row_ = cursor_row_;
    anchor_col_ = cursor_col_;
    pending_key_ = 0;
}

void ViCopyMode::exitCopyMode() {
    active_ = false;
    mode_ = ViMode::Normal;
    pending_key_ = 0;
}

ViAction ViCopyMode::processKey(char key, bool ctrl, bool shift) {
    if (!active_) return ViAction::None;

    // Handle pending multi-key commands
    if (pending_key_ == 'g') {
        pending_key_ = 0;
        if (key == 'g') moveToTop();
        return ViAction::None;
    }
    if (pending_key_ == 'f' || pending_key_ == 'F') {
        int dir = (pending_key_ == 'f') ? 1 : -1;
        pending_key_ = 0;
        for (int c = cursor_col_ + dir; c >= 0 && c < screen_.cols(); c += dir) {
            if (static_cast<char>(cellAt(cursor_row_, c)) == key) {
                cursor_col_ = c;
                break;
            }
        }
        return ViAction::None;
    }

    // Ctrl-V: block visual toggle
    if (ctrl && (key == 'v' || key == 'V')) {
        if (mode_ == ViMode::VisualBlock) { mode_ = ViMode::Normal; }
        else { mode_ = ViMode::VisualBlock; anchor_row_ = cursor_row_; anchor_col_ = cursor_col_; }
        return ViAction::None;
    }
    if (ctrl) return ViAction::None;

    switch (key) {
    case 'v':
        if (shift) {
            if (mode_ == ViMode::VisualLine) mode_ = ViMode::Normal;
            else { mode_ = ViMode::VisualLine; anchor_row_ = cursor_row_; anchor_col_ = cursor_col_; }
        } else {
            if (mode_ == ViMode::Visual) mode_ = ViMode::Normal;
            else { mode_ = ViMode::Visual; anchor_row_ = cursor_row_; anchor_col_ = cursor_col_; }
        }
        return ViAction::None;
    case 'h': moveLeft(); return ViAction::None;
    case 'j': moveDown(); return ViAction::None;
    case 'k': moveUp(); return ViAction::None;
    case 'l': moveRight(); return ViAction::None;
    case 'w': moveWordForward(); return ViAction::None;
    case 'b': moveWordBackward(); return ViAction::None;
    case 'e': moveWordEnd(); return ViAction::None;
    case '0': moveLineBegin(); return ViAction::None;
    case '$': moveLineEnd(); return ViAction::None;
    case '^': moveFirstNonBlank(); return ViAction::None;
    case 'g': pending_key_ = 'g'; return ViAction::None;
    case 'G': moveToBottom(); return ViAction::None;
    case 'H': moveScreenTop(); return ViAction::None;
    case 'M': moveScreenMiddle(); return ViAction::None;
    case 'L': moveScreenBottom(); return ViAction::None;
    case '{': moveParagraphUp(); return ViAction::None;
    case '}': moveParagraphDown(); return ViAction::None;
    case 'f': pending_key_ = 'f'; return ViAction::None;
    case 'F': pending_key_ = 'F'; return ViAction::None;
    case '/': return ViAction::SearchForward;
    case '?': return ViAction::SearchBackward;
    case 'y': return (mode_ != ViMode::Normal) ? ViAction::Yank : ViAction::None;
    case 'q': case '\x1b': exitCopyMode(); return ViAction::Exit;
    default: return ViAction::None;
    }
}

ViSelection ViCopyMode::getSelection() const {
    if (!active_ || mode_ == ViMode::Normal)
        return {cursor_row_, cursor_col_, cursor_row_, cursor_col_, mode_};
    int r0 = anchor_row_, c0 = anchor_col_, r1 = cursor_row_, c1 = cursor_col_;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) { std::swap(r0, r1); std::swap(c0, c1); }
    return {r0, c0, r1, c1, mode_};
}

void ViCopyMode::selectAll() {
    mode_ = ViMode::Visual;
    anchor_row_ = -static_cast<int>(screen_.scrollbackSize());
    anchor_col_ = 0;
    cursor_row_ = screen_.rows() - 1;
    cursor_col_ = screen_.cols() - 1;
}

std::string ViCopyMode::yankSelection() const {
    if (!active_ || mode_ == ViMode::Normal) return {};
    auto sel = getSelection();
    if (mode_ == ViMode::VisualLine) return extractLineText(sel.start_row, sel.end_row);
    if (mode_ == ViMode::VisualBlock) return extractBlockText(sel.start_row, sel.start_col, sel.end_row, sel.end_col);
    return extractText(sel.start_row, sel.start_col, sel.end_row, sel.end_col);
}

// --- Motions ---

void ViCopyMode::moveLeft() { if (cursor_col_ > 0) --cursor_col_; }
void ViCopyMode::moveRight() { if (cursor_col_ < screen_.cols() - 1) ++cursor_col_; }

void ViCopyMode::moveUp() {
    int min_row = -static_cast<int>(screen_.scrollbackSize());
    if (cursor_row_ > min_row) { --cursor_row_; clampCursor(); }
}

void ViCopyMode::moveDown() {
    if (cursor_row_ < screen_.rows() - 1) { ++cursor_row_; clampCursor(); }
}

void ViCopyMode::moveWordForward() {
    int cols = screen_.cols(), max_row = screen_.rows() - 1;
    bool in_word = isWordChar(cellAt(cursor_row_, cursor_col_));
    while (cursor_row_ <= max_row) {
        if (cursor_col_ < cols - 1) ++cursor_col_;
        else if (cursor_row_ < max_row) { ++cursor_row_; cursor_col_ = 0; }
        else break;
        bool cur = isWordChar(cellAt(cursor_row_, cursor_col_));
        if (in_word && !cur) in_word = false;
        else if (!in_word && cur) break;
    }
}

void ViCopyMode::moveWordBackward() {
    int min_row = -static_cast<int>(screen_.scrollbackSize());
    // Step back one
    if (cursor_col_ > 0) --cursor_col_;
    else if (cursor_row_ > min_row) { --cursor_row_; cursor_col_ = screen_.cols() - 1; }
    // Skip non-word
    while (!isWordChar(cellAt(cursor_row_, cursor_col_))) {
        if (cursor_col_ > 0) --cursor_col_;
        else if (cursor_row_ > min_row) { --cursor_row_; cursor_col_ = screen_.cols() - 1; }
        else return;
    }
    // Move to word start
    while (true) {
        int pc = cursor_col_ - 1, pr = cursor_row_;
        if (pc < 0) { pr--; if (pr < min_row) break; pc = screen_.cols() - 1; }
        if (!isWordChar(cellAt(pr, pc))) break;
        cursor_col_ = pc; cursor_row_ = pr;
    }
}

void ViCopyMode::moveWordEnd() {
    int cols = screen_.cols(), max_row = screen_.rows() - 1;
    if (cursor_col_ < cols - 1) ++cursor_col_;
    else if (cursor_row_ < max_row) { ++cursor_row_; cursor_col_ = 0; }
    while (!isWordChar(cellAt(cursor_row_, cursor_col_))) {
        if (cursor_col_ < cols - 1) ++cursor_col_;
        else if (cursor_row_ < max_row) { ++cursor_row_; cursor_col_ = 0; }
        else return;
    }
    while (true) {
        int nc = cursor_col_ + 1, nr = cursor_row_;
        if (nc >= cols) { nr++; if (nr > max_row) break; nc = 0; }
        if (!isWordChar(cellAt(nr, nc))) break;
        cursor_col_ = nc; cursor_row_ = nr;
    }
}

void ViCopyMode::moveLineBegin() { cursor_col_ = 0; }
void ViCopyMode::moveLineEnd() { cursor_col_ = screen_.cols() - 1; }

void ViCopyMode::moveFirstNonBlank() {
    cursor_col_ = 0;
    while (cursor_col_ < screen_.cols() - 1) {
        char32_t ch = cellAt(cursor_row_, cursor_col_);
        if (ch != ' ' && ch != '\t') break;
        ++cursor_col_;
    }
}

void ViCopyMode::moveToTop() {
    cursor_row_ = -static_cast<int>(screen_.scrollbackSize());
    cursor_col_ = 0;
}

void ViCopyMode::moveToBottom() { cursor_row_ = screen_.rows() - 1; cursor_col_ = 0; }
void ViCopyMode::moveScreenTop() { cursor_row_ = 0; }
void ViCopyMode::moveScreenMiddle() { cursor_row_ = screen_.rows() / 2; }
void ViCopyMode::moveScreenBottom() { cursor_row_ = screen_.rows() - 1; }

void ViCopyMode::moveParagraphUp() {
    int min_row = -static_cast<int>(screen_.scrollbackSize());
    while (cursor_row_ > min_row) {
        --cursor_row_;
        bool blank = true;
        for (int c = 0; c < screen_.cols() && blank; ++c) {
            char32_t ch = cellAt(cursor_row_, c);
            if (ch != ' ' && ch != 0) blank = false;
        }
        if (blank) break;
    }
    cursor_col_ = 0;
}

void ViCopyMode::moveParagraphDown() {
    int max_row = screen_.rows() - 1;
    while (cursor_row_ < max_row) {
        ++cursor_row_;
        bool blank = true;
        for (int c = 0; c < screen_.cols() && blank; ++c) {
            char32_t ch = cellAt(cursor_row_, c);
            if (ch != ' ' && ch != 0) blank = false;
        }
        if (blank) break;
    }
    cursor_col_ = 0;
}

// --- Helpers ---

void ViCopyMode::clampCursor() {
    int min_row = -static_cast<int>(screen_.scrollbackSize());
    cursor_row_ = std::clamp(cursor_row_, min_row, screen_.rows() - 1);
    cursor_col_ = std::clamp(cursor_col_, 0, screen_.cols() - 1);
}

char32_t ViCopyMode::cellAt(int row, int col) const {
    if (row < 0) {
        int sb_idx = static_cast<int>(screen_.scrollbackSize()) + row;
        if (sb_idx < 0 || sb_idx >= static_cast<int>(screen_.scrollbackSize())) return ' ';
        std::string line = screen_.getScrollbackLineText(
            static_cast<int>(screen_.scrollbackSize()) - 1 - sb_idx);
        if (col < 0 || col >= static_cast<int>(line.size())) return ' ';
        return static_cast<char32_t>(static_cast<unsigned char>(line[col]));
    }
    return screen_.cellAt(row, col).codepoint;
}

bool ViCopyMode::isWordChar(char32_t ch) const {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_';
}

void ViCopyMode::appendUtf8(std::string& out, char32_t ch) const {
    if (ch == 0) ch = ' ';
    if (ch < 0x80) { out += static_cast<char>(ch); }
    else if (ch < 0x800) { out += static_cast<char>(0xC0 | (ch >> 6)); out += static_cast<char>(0x80 | (ch & 0x3F)); }
    else if (ch < 0x10000) { out += static_cast<char>(0xE0 | (ch >> 12)); out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F)); out += static_cast<char>(0x80 | (ch & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (ch >> 18)); out += static_cast<char>(0x80 | ((ch >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F)); out += static_cast<char>(0x80 | (ch & 0x3F)); }
}

std::string ViCopyMode::extractText(int r0, int c0, int r1, int c1) const {
    std::string out;
    for (int r = r0; r <= r1; ++r) {
        int start = (r == r0) ? c0 : 0;
        int end = (r == r1) ? c1 : screen_.cols() - 1;
        for (int c = start; c <= end; ++c) appendUtf8(out, cellAt(r, c));
        if (r < r1) out += '\n';
    }
    return out;
}

std::string ViCopyMode::extractLineText(int r0, int r1) const {
    std::string out;
    for (int r = r0; r <= r1; ++r) {
        for (int c = 0; c < screen_.cols(); ++c) appendUtf8(out, cellAt(r, c));
        if (r < r1) out += '\n';
    }
    return out;
}

std::string ViCopyMode::extractBlockText(int r0, int c0, int r1, int c1) const {
    int left = std::min(c0, c1), right = std::max(c0, c1);
    std::string out;
    for (int r = r0; r <= r1; ++r) {
        for (int c = left; c <= right; ++c) appendUtf8(out, cellAt(r, c));
        if (r < r1) out += '\n';
    }
    return out;
}

} // namespace termcore
