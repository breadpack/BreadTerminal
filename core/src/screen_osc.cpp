#include "termcore/screen.h"
#include <algorithm>
#include <optional>
#include <sstream>

namespace termcore {

// ---------------------------------------------------------------------------
// X11 color spec parsing / formatting  (for OSC 4/10-19)
// ---------------------------------------------------------------------------

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool isHex(char c) { return hexVal(c) >= 0; }

/// Parse an X11 color spec: "rgb:RR/GG/BB", "rgb:RRRR/GGGG/BBBB", "#RRGGBB"
static std::optional<uint32_t> parseXColorSpec(const std::string& spec) {
    if (spec.empty()) return std::nullopt;

    // "#RRGGBB"
    if (spec[0] == '#') {
        if (spec.size() == 7) {
            uint32_t color = 0;
            for (int i = 1; i <= 6; ++i) {
                int v = hexVal(spec[i]);
                if (v < 0) return std::nullopt;
                color = (color << 4) | v;
            }
            return color;
        }
        return std::nullopt;
    }

    // "rgb:..." or "rgbi:..." (we only handle rgb:)
    if (spec.size() < 5 || spec.compare(0, 4, "rgb:") != 0)
        return std::nullopt;

    // Split on '/'
    std::string rest = spec.substr(4);
    auto slash1 = rest.find('/');
    if (slash1 == std::string::npos) return std::nullopt;
    auto slash2 = rest.find('/', slash1 + 1);
    if (slash2 == std::string::npos) return std::nullopt;

    std::string rs = rest.substr(0, slash1);
    std::string gs = rest.substr(slash1 + 1, slash2 - slash1 - 1);
    std::string bs = rest.substr(slash2 + 1);

    if (rs.empty() || gs.empty() || bs.empty()) return std::nullopt;

    // All components must be same length (1, 2, 3, or 4 hex digits)
    auto parseComponent = [](const std::string& s) -> int {
        if (s.size() < 1 || s.size() > 4) return -1;
        int val = 0;
        for (char c : s) {
            int h = hexVal(c);
            if (h < 0) return -1;
            val = (val << 4) | h;
        }
        // Scale to 8-bit: take the most significant 8 bits
        switch (s.size()) {
        case 1: return val * 0x11;           // 0x0 -> 0x00, 0xF -> 0xFF
        case 2: return val;                   // already 8-bit
        case 3: return val >> 4;              // 12-bit -> 8-bit
        case 4: return val >> 8;              // 16-bit -> 8-bit
        default: return -1;
        }
    };

    int r = parseComponent(rs);
    int g = parseComponent(gs);
    int b = parseComponent(bs);
    if (r < 0 || g < 0 || b < 0) return std::nullopt;

    return static_cast<uint32_t>((r << 16) | (g << 8) | b);
}

/// Format a 24-bit RGB color as "rgb:RRRR/GGGG/BBBB" (16-bit per component).
static std::string formatXColor(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    // Expand 8-bit to 16-bit: v -> (v << 8) | v
    char buf[32];
    snprintf(buf, sizeof(buf), "rgb:%04x/%04x/%04x",
             (r << 8) | r, (g << 8) | g, (b << 8) | b);
    return buf;
}

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

// --- OSC 4: Set/query palette color ---
// Format: OSC 4 ; index ; spec ST
// Multiple pairs: OSC 4 ; i1 ; spec1 ; i2 ; spec2 ... ST
// Query: OSC 4 ; index ; ? ST
void Screen::handleOscPaletteColor(const std::string& str) {
    // Split on ';'
    std::vector<std::string> parts;
    std::string::size_type start = 0;
    while (start <= str.size()) {
        auto sep = str.find(';', start);
        if (sep == std::string::npos) {
            parts.push_back(str.substr(start));
            break;
        }
        parts.push_back(str.substr(start, sep - start));
        start = sep + 1;
    }

    // Process pairs (index, spec)
    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        int idx = -1;
        try { idx = std::stoi(parts[i]); } catch (...) { continue; }
        if (idx < 0 || idx > 255) continue;

        const std::string& spec = parts[i + 1];
        if (spec == "?") {
            // Query: respond with current color
            if (response_callback_) {
                std::string resp = "\033]4;" + std::to_string(idx) + ";" +
                                   formatXColor(dynamic_colors_.palette[idx]) + "\033\\";
                response_callback_(resp);
            }
        } else {
            auto color = parseXColorSpec(spec);
            if (color) {
                dynamic_colors_.palette[idx] = *color;
                if (dynamic_color_callback_) {
                    dynamic_color_callback_({-1, *color});
                }
            }
        }
    }
}

// --- OSC 10-19: Set/query dynamic colors ---
// Format: OSC N ; spec ST   (where N = 10..19)
// Query:  OSC N ; ? ST
// Chained: OSC 10 ; spec1 ; spec2 ; spec3 ST  (sets 10, 11, 12, ...)
void Screen::handleOscDynamicColor(int osc_number, const std::string& str) {
    int base_slot = osc_number - 10;

    // Split on ';'
    std::vector<std::string> parts;
    std::string::size_type start = 0;
    while (start <= str.size()) {
        auto sep = str.find(';', start);
        if (sep == std::string::npos) {
            parts.push_back(str.substr(start));
            break;
        }
        parts.push_back(str.substr(start, sep - start));
        start = sep + 1;
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        int slot = base_slot + static_cast<int>(i);
        if (slot < 0 || slot > 9) break;

        const std::string& spec = parts[i];
        if (spec == "?") {
            // Query
            if (response_callback_) {
                // Access the slot value
                const uint32_t* slotPtr = nullptr;
                switch (slot) {
                case 0: slotPtr = &dynamic_colors_.foreground; break;
                case 1: slotPtr = &dynamic_colors_.background; break;
                case 2: slotPtr = &dynamic_colors_.cursor_color; break;
                case 3: slotPtr = &dynamic_colors_.mouse_fg; break;
                case 4: slotPtr = &dynamic_colors_.mouse_bg; break;
                case 5: slotPtr = &dynamic_colors_.tek_fg; break;
                case 6: slotPtr = &dynamic_colors_.tek_bg; break;
                case 7: slotPtr = &dynamic_colors_.highlight_bg; break;
                case 8: slotPtr = &dynamic_colors_.bold_color; break;
                case 9: slotPtr = &dynamic_colors_.italic_color; break;
                }
                if (slotPtr) {
                    std::string resp = "\033]" + std::to_string(10 + slot) + ";" +
                                       formatXColor(*slotPtr) + "\033\\";
                    response_callback_(resp);
                }
            }
        } else {
            auto color = parseXColorSpec(spec);
            if (color) {
                switch (slot) {
                case 0: dynamic_colors_.foreground = *color; break;
                case 1: dynamic_colors_.background = *color; break;
                case 2: dynamic_colors_.cursor_color = *color; break;
                case 3: dynamic_colors_.mouse_fg = *color; break;
                case 4: dynamic_colors_.mouse_bg = *color; break;
                case 5: dynamic_colors_.tek_fg = *color; break;
                case 6: dynamic_colors_.tek_bg = *color; break;
                case 7: dynamic_colors_.highlight_bg = *color; break;
                case 8: dynamic_colors_.bold_color = *color; break;
                case 9: dynamic_colors_.italic_color = *color; break;
                }
                if (dynamic_color_callback_) {
                    dynamic_color_callback_({slot, *color});
                }
            }
        }
    }
}

// --- OSC 104 / 110-119: Reset colors ---
// OSC 104 ; idx1 ; idx2 ... ST   (reset palette entries, or all if no args)
// OSC 110-119 ST                  (reset dynamic colors)
void Screen::handleOscResetColor(int osc_number, const std::string& str) {
    if (osc_number == 104) {
        if (str.empty()) {
            // Reset all palette entries
            dynamic_colors_.resetAllPalette();
            if (dynamic_color_callback_) {
                dynamic_color_callback_({-1, 0});
            }
        } else {
            // Reset specific entries
            std::string::size_type start = 0;
            while (start <= str.size()) {
                auto sep = str.find(';', start);
                std::string part;
                if (sep == std::string::npos) {
                    part = str.substr(start);
                    start = str.size() + 1;
                } else {
                    part = str.substr(start, sep - start);
                    start = sep + 1;
                }
                if (!part.empty()) {
                    int idx = -1;
                    try { idx = std::stoi(part); } catch (...) { continue; }
                    if (idx >= 0 && idx <= 255) {
                        dynamic_colors_.resetPaletteEntry(idx);
                        if (dynamic_color_callback_) {
                            dynamic_color_callback_({-1, dynamic_colors_.palette[idx]});
                        }
                    }
                }
            }
        }
    } else if (osc_number >= 110 && osc_number <= 119) {
        int slot = osc_number - 110;
        dynamic_colors_.resetDynamic(slot);
        if (dynamic_color_callback_) {
            dynamic_color_callback_({slot, 0});
        }
    }
}

} // namespace termcore
