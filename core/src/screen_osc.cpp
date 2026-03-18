#include "termcore/screen.h"
#include <algorithm>

namespace termcore {

// --- URL decoding helper ---
static std::string urlDecode(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            auto hexToNibble = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hexToNibble(str[i + 1]);
            int lo = hexToNibble(str[i + 2]);
            if (hi >= 0 && lo >= 0) {
                result += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        result += str[i];
    }
    return result;
}

// --- OSC 7: Working directory ---
void Screen::handleOscWorkingDirectory(const std::string& str) {
    // Format: file://hostname/path/to/dir
    // or:     file:///path/to/dir
    const std::string prefix = "file://";
    if (str.size() < prefix.size() ||
        str.compare(0, prefix.size(), prefix) != 0) {
        // Not a valid file URL; store raw
        working_directory_ = str;
        return;
    }
    // Skip "file://"
    auto rest = str.substr(prefix.size());
    // Find the path (starts at next '/')
    auto slash_pos = rest.find('/');
    if (slash_pos == std::string::npos) {
        working_directory_ = urlDecode(rest);
    } else {
        working_directory_ = urlDecode(rest.substr(slash_pos));
    }
}

// --- OSC 8: Hyperlink ---
void Screen::handleOscHyperlink(const std::string& str) {
    // Format: params;uri
    // params can be empty or contain key=value pairs
    auto semi = str.find(';');
    if (semi == std::string::npos) {
        // Malformed; treat entire string as URI
        current_hyperlink_ = str;
        return;
    }
    auto uri = str.substr(semi + 1);
    if (uri.empty()) {
        current_hyperlink_.clear();
    } else {
        current_hyperlink_ = uri;
    }
}

// --- OSC 52: Clipboard ---
void Screen::handleOscClipboard(const std::string& str) {
    // Format: selection;base64_data
    // selection is typically 'c' (clipboard) or 'p' (primary)
    auto semi = str.find(';');
    if (semi == std::string::npos) return;

    char selection = 'c';
    if (semi > 0) {
        selection = str[0];
    }
    auto data = str.substr(semi + 1);

    ClipboardEvent event;
    event.selection = selection;

    if (data == "?") {
        // Read request
        event.is_read = true;
        if (clipboard_callback_) {
            clipboard_callback_(event);
        }
    } else {
        // Write request - data is base64 encoded
        event.is_read = false;
        event.data = data;
        if (clipboard_callback_) {
            clipboard_callback_(event);
        }
    }
}

// --- OSC 9/99/777: Notifications ---
void Screen::handleOscNotification(int type, const std::string& str) {
    TermNotification notif;
    notif.type = type;

    if (type == 777) {
        // Format: notify;title;body
        // First field is the command (usually "notify")
        auto first_semi = str.find(';');
        if (first_semi != std::string::npos) {
            auto second_semi = str.find(';', first_semi + 1);
            if (second_semi != std::string::npos) {
                notif.title = str.substr(first_semi + 1,
                                         second_semi - first_semi - 1);
                notif.body = str.substr(second_semi + 1);
            } else {
                notif.title = str.substr(first_semi + 1);
            }
        } else {
            notif.body = str;
        }
    } else if (type == 99) {
        // Kitty notification: various sub-formats
        // Simple case: just the message body
        notif.body = str;
    } else {
        // OSC 9: str is the message
        notif.body = str;
    }

    last_notification_ = notif;
    if (notification_callback_) {
        notification_callback_(notif);
    }
}

// --- OSC 133: Shell integration ---
void Screen::handleOscShellIntegration(const std::string& str) {
    // Markers: A (prompt start), B (prompt end/input start),
    //          C (input end/output start), D (output end)
    if (str.empty()) return;

    switch (str[0]) {
    case 'A':
        prompt_state_ = PromptState::Prompt;
        break;
    case 'B':
        prompt_state_ = PromptState::Input;
        break;
    case 'C':
        prompt_state_ = PromptState::Output;
        break;
    case 'D':
        prompt_state_ = PromptState::None;
        break;
    default:
        break;
    }
}

} // namespace termcore
