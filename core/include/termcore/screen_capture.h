#ifndef TERMCORE_SCREEN_CAPTURE_H
#define TERMCORE_SCREEN_CAPTURE_H

#include "termcore/screen.h"
#include <string>
#include <vector>

namespace termcore {

/// Utility for extracting text content from a terminal Screen.
class ScreenCapture {
public:
    /// Capture the last N visible lines from the screen (bottom-up).
    /// Returns lines joined by newline characters.
    static std::string captureLines(const Screen& screen, int line_count);

    /// Capture entire visible viewport as text.
    static std::string captureViewport(const Screen& screen);

    /// Capture scrollback (last N lines from scrollback buffer).
    /// line 0 = most recent scrollback line.
    static std::string captureScrollback(const Screen& screen, int line_count);

    /// A single captured line with metadata.
    struct CapturedLine {
        int row;
        std::string text;
        bool is_prompt;  // true if this row is near an OSC 133 prompt marker
    };

    /// Capture the last N visible lines as structured data.
    static std::vector<CapturedLine> captureStructured(const Screen& screen, int line_count);

    /// Cursor position information.
    struct CursorInfo {
        int row;
        int col;
        bool visible;
    };

    /// Get current cursor position and visibility.
    static CursorInfo getCursorInfo(const Screen& screen);
};

} // namespace termcore

#endif // TERMCORE_SCREEN_CAPTURE_H
