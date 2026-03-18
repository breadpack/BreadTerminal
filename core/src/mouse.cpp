#include "termcore/mouse.h"
#include "termcore/screen.h"

#include <string>

namespace termcore {

namespace {

/// Compute the button byte (before adding the +32 legacy offset).
int computeButtonCode(const MouseEvent& event) {
    int code = 0;

    switch (event.type) {
    case MouseEventType::ScrollUp:
        code = 64 + 0;
        break;
    case MouseEventType::ScrollDown:
        code = 64 + 1;
        break;
    case MouseEventType::Press:
    case MouseEventType::Release:
        switch (event.button) {
        case MouseButton::Left:      code = 0; break;
        case MouseButton::Middle:    code = 1; break;
        case MouseButton::Right:     code = 2; break;
        case MouseButton::Release:   code = 3; break;
        case MouseButton::ScrollUp:  code = 64 + 0; break;
        case MouseButton::ScrollDown:code = 64 + 1; break;
        case MouseButton::ScrollLeft:code = 64 + 2; break;
        case MouseButton::ScrollRight:code = 64 + 3; break;
        }
        // For release in default encoding, button code is 3
        if (event.type == MouseEventType::Release) {
            code = 3;
        }
        break;
    case MouseEventType::Move:
        switch (event.button) {
        case MouseButton::Left:      code = 0; break;
        case MouseButton::Middle:    code = 1; break;
        case MouseButton::Right:     code = 2; break;
        default:                     code = 3; break;
        }
        code |= 32; // motion flag
        break;
    }

    // Modifier keys
    if (event.shift) code |= 4;
    if (event.alt)   code |= 8;
    if (event.ctrl)  code |= 16;

    return code;
}

/// Check whether the event should be reported given the current mouse mode.
bool shouldReport(const MouseEvent& event, MouseMode mode) {
    if (mode == MouseMode::None) return false;

    switch (mode) {
    case MouseMode::X10:
        // X10: only button presses (no release, no motion)
        return event.type == MouseEventType::Press
            || event.type == MouseEventType::ScrollUp
            || event.type == MouseEventType::ScrollDown;

    case MouseMode::ButtonEvent:
        // Button-event: presses, releases, scroll, and drag (motion with button)
        if (event.type == MouseEventType::Move) {
            // Only report motion if a real button is held
            return event.button != MouseButton::Release;
        }
        return true;

    case MouseMode::AnyEvent:
        // All events reported
        return true;

    default:
        return false;
    }
}

std::string encodeDefault(int buttonCode, int col, int row) {
    std::string result;
    result.reserve(6);
    result += '\033';
    result += '[';
    result += 'M';
    result += static_cast<char>(buttonCode + 32);
    result += static_cast<char>(col + 1 + 32);  // 1-based + 32 offset
    result += static_cast<char>(row + 1 + 32);
    return result;
}

std::string encodeSGR(int buttonCode, int col, int row, bool release) {
    // \033[< button ; col ; row M/m
    std::string result;
    result += "\033[<";
    result += std::to_string(buttonCode);
    result += ';';
    result += std::to_string(col + 1);  // 1-based
    result += ';';
    result += std::to_string(row + 1);  // 1-based
    result += release ? 'm' : 'M';
    return result;
}

} // anonymous namespace

std::string encodeMouseEvent(const MouseEvent& event,
                              MouseMode mode,
                              MouseEncoding encoding) {
    if (!shouldReport(event, mode)) {
        return "";
    }

    int buttonCode = computeButtonCode(event);
    bool isRelease = (event.type == MouseEventType::Release);

    switch (encoding) {
    case MouseEncoding::SGR:
        // For SGR, use the actual button code (not 3 for release)
        if (isRelease) {
            // Recompute without the release override
            int sgrCode = 0;
            switch (event.button) {
            case MouseButton::Left:      sgrCode = 0; break;
            case MouseButton::Middle:    sgrCode = 1; break;
            case MouseButton::Right:     sgrCode = 2; break;
            default:                     sgrCode = 0; break;
            }
            if (event.shift) sgrCode |= 4;
            if (event.alt)   sgrCode |= 8;
            if (event.ctrl)  sgrCode |= 16;
            return encodeSGR(sgrCode, event.col, event.row, true);
        }
        return encodeSGR(buttonCode, event.col, event.row, false);

    case MouseEncoding::Default:
    default:
        return encodeDefault(buttonCode, event.col, event.row);
    }
}

} // namespace termcore
