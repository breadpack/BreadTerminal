#include "termcore/screen_capture.h"
#include <algorithm>

namespace termcore {

// --- captureLines ---
std::string ScreenCapture::captureLines(const Screen& screen, int line_count) {
    int total_rows = screen.rows();
    int count = std::min(line_count, total_rows);
    if (count <= 0) return "";

    int start_row = total_rows - count;
    std::string result;
    for (int r = start_row; r < total_rows; ++r) {
        if (r > start_row) result += '\n';
        result += screen.getLineText(r);
    }
    return result;
}

// --- captureViewport ---
std::string ScreenCapture::captureViewport(const Screen& screen) {
    return captureLines(screen, screen.rows());
}

// --- captureScrollback ---
std::string ScreenCapture::captureScrollback(const Screen& screen, int line_count) {
    int sb_size = static_cast<int>(screen.scrollbackSize());
    int count = std::min(line_count, sb_size);
    if (count <= 0) return "";

    std::string result;
    // line 0 = most recent scrollback line, iterate from oldest to newest
    for (int i = count - 1; i >= 0; --i) {
        if (i < count - 1) result += '\n';
        result += screen.getScrollbackLineText(i);
    }
    return result;
}

// --- captureStructured ---
std::vector<ScreenCapture::CapturedLine>
ScreenCapture::captureStructured(const Screen& screen, int line_count) {
    int total_rows = screen.rows();
    int count = std::min(line_count, total_rows);
    if (count <= 0) return {};

    int start_row = total_rows - count;

    // Build a set of prompt rows using nextPromptRow/prevPromptRow.
    // Walk from row 0 forward to find all prompt markers in the visible area.
    std::vector<bool> is_prompt_row(total_rows, false);
    int pr = screen.nextPromptRow(-1);
    while (pr >= 0 && pr < total_rows) {
        is_prompt_row[pr] = true;
        int next = screen.nextPromptRow(pr);
        if (next <= pr) break;  // avoid infinite loop
        pr = next;
    }
    // Also check previousPromptRow from the bottom
    pr = screen.previousPromptRow(total_rows);
    while (pr >= 0 && pr < total_rows) {
        is_prompt_row[pr] = true;
        int prev = screen.previousPromptRow(pr);
        if (prev >= pr) break;
        pr = prev;
    }

    std::vector<CapturedLine> lines;
    lines.reserve(count);
    for (int r = start_row; r < total_rows; ++r) {
        CapturedLine cl;
        cl.row = r;
        cl.text = screen.getLineText(r);
        cl.is_prompt = is_prompt_row[r];
        lines.push_back(std::move(cl));
    }
    return lines;
}

// --- getCursorInfo ---
ScreenCapture::CursorInfo ScreenCapture::getCursorInfo(const Screen& screen) {
    CursorInfo info;
    info.row = screen.cursorRow();
    info.col = screen.cursorCol();
    info.visible = screen.cursorVisible();
    return info;
}

} // namespace termcore
