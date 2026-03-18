#ifndef TERMCORE_MOUSE_H
#define TERMCORE_MOUSE_H

#include <cstdint>
#include <string>

namespace termcore {

enum class MouseButton : uint8_t {
    Left = 0,
    Middle = 1,
    Right = 2,
    Release = 3,    // Button release (X10 style)
    ScrollUp = 4,
    ScrollDown = 5,
    ScrollLeft = 6,
    ScrollRight = 7,
};

enum class MouseEventType : uint8_t {
    Press,
    Release,
    Move,       // Motion while button held (drag)
    ScrollUp,
    ScrollDown,
};

struct MouseEvent {
    MouseEventType type;
    MouseButton button;
    int col;        // 0-based column
    int row;        // 0-based row
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
};

enum class MouseMode : uint8_t;
enum class MouseEncoding : uint8_t;

/// Encode a mouse event into the appropriate escape sequence.
/// Returns empty string if mouse reporting is not active.
std::string encodeMouseEvent(const MouseEvent& event,
                              MouseMode mode,
                              MouseEncoding encoding);

} // namespace termcore
#endif // TERMCORE_MOUSE_H
