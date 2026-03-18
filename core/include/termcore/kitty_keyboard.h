#ifndef TERMCORE_KITTY_KEYBOARD_H
#define TERMCORE_KITTY_KEYBOARD_H
#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

enum KittyKeyFlag : uint32_t {
    KittyDisambiguate = 1,
    KittyReportEvents = 2,
    KittyReportAlternate = 4,
    KittyReportAll = 8,
    KittyReportText = 16,
};

enum class KittyEventType : uint8_t { Press = 1, Repeat = 2, Release = 3 };

struct KittyKeyEvent {
    uint32_t key_code;
    uint32_t shifted_key = 0;
    uint32_t base_key = 0;
    uint8_t modifiers = 0;
    KittyEventType event_type = KittyEventType::Press;
    std::string text;
};

std::string encodeKittyKey(const KittyKeyEvent& event, uint32_t flags);

class KittyKeyboardState {
public:
    void pushMode(uint32_t flags);
    void popMode(int count = 1);
    uint32_t currentFlags() const;
    bool isActive() const { return !stack_.empty(); }
    size_t stackDepth() const { return stack_.size(); }
    void reset();

private:
    std::vector<uint32_t> stack_;
};

namespace KittyKey {
    constexpr uint32_t Escape = 27, Enter = 13, Tab = 9, Backspace = 127;
    constexpr uint32_t Left = 57414, Right = 57415, Up = 57416, Down = 57417;
    constexpr uint32_t Home = 57418, End = 57419, PageUp = 57420, PageDown = 57421;
    constexpr uint32_t F1 = 57364, F2 = 57365, F3 = 57366, F4 = 57367;
    constexpr uint32_t F5 = 57368, F6 = 57369, F7 = 57370, F8 = 57371;
    constexpr uint32_t F9 = 57372, F10 = 57373, F11 = 57374, F12 = 57375;
} // namespace KittyKey

} // namespace termcore
#endif // TERMCORE_KITTY_KEYBOARD_H
