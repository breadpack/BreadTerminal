#include "termcore/kitty_keyboard.h"
#include <algorithm>

namespace termcore {

void KittyKeyboardState::pushMode(uint32_t flags) {
    stack_.push_back(flags);
}

void KittyKeyboardState::popMode(int count) {
    int to_pop = std::min(count, static_cast<int>(stack_.size()));
    for (int i = 0; i < to_pop; ++i) {
        stack_.pop_back();
    }
}

uint32_t KittyKeyboardState::currentFlags() const {
    if (stack_.empty())
        return 0;
    return stack_.back();
}

void KittyKeyboardState::reset() {
    stack_.clear();
}

std::string encodeKittyKey(const KittyKeyEvent& event, uint32_t flags) {
    if (flags == 0)
        return "";

    // Build CSI sequence: \033[keycode;modifiers[+1]u
    // With optional :event_type suffix on modifiers
    std::string result = "\033[";
    result += std::to_string(event.key_code);

    uint8_t mods = event.modifiers + 1; // Kitty protocol: modifier value = modifiers + 1

    bool has_shifted = (flags & KittyReportAlternate) && event.shifted_key != 0;
    bool has_base = (flags & KittyReportAlternate) && event.base_key != 0;
    bool has_event = (flags & KittyReportEvents) &&
                     event.event_type != KittyEventType::Press;
    bool has_text = (flags & KittyReportText) && !event.text.empty();

    // Append shifted_key and base_key after key_code with ':' separator
    if (has_shifted || has_base) {
        result += ':';
        if (has_shifted)
            result += std::to_string(event.shifted_key);
        if (has_base) {
            result += ':';
            result += std::to_string(event.base_key);
        }
    }

    // Always need semicolon + modifiers if modifiers > 1 or event_type/text present
    if (mods > 1 || has_event || has_text) {
        result += ';';
        result += std::to_string(mods);

        if (has_event) {
            result += ':';
            result += std::to_string(static_cast<int>(event.event_type));
        }
    }

    // Append text payload
    if (has_text) {
        result += ';';
        // Encode each char of text as its Unicode codepoint
        for (size_t i = 0; i < event.text.size(); ++i) {
            if (i > 0)
                result += ':';
            result += std::to_string(static_cast<uint32_t>(
                static_cast<unsigned char>(event.text[i])));
        }
    }

    result += 'u';
    return result;
}

} // namespace termcore
