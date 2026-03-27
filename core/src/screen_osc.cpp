#include "termcore/screen.h"
#include "termcore/iterm_image.h"
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
        remote_hostname_.clear();
    } else {
        // Extract hostname (between "file://" and the first '/')
        std::string hostname = rest.substr(0, slash_pos);
        if (!hostname.empty() && hostname != "localhost") {
            remote_hostname_ = hostname;
        } else {
            remote_hostname_.clear();
        }
        working_directory_ = urlDecode(rest.substr(slash_pos));
    }
}

// --- URI scheme validation helper ---
static bool isAllowedUriScheme(const std::string& uri) {
    // Extract scheme (everything before "://") or before ":"
    auto colon = uri.find(':');
    if (colon == std::string::npos || colon == 0) return false;

    std::string scheme;
    scheme.reserve(colon);
    for (size_t i = 0; i < colon; ++i) {
        scheme += static_cast<char>(std::tolower(static_cast<unsigned char>(uri[i])));
    }

    // Allow only safe schemes
    return scheme == "http" || scheme == "https" ||
           scheme == "mailto" || scheme == "ssh" || scheme == "file";
}

// --- OSC 8: Hyperlink ---
void Screen::handleOscHyperlink(const std::string& str) {
    // Format: params;uri
    // params can be empty or contain key=value pairs
    auto semi = str.find(';');
    if (semi == std::string::npos) {
        // Malformed; treat entire string as URI
        if (isAllowedUriScheme(str)) {
            current_hyperlink_ = str;
        } else {
            current_hyperlink_.clear();
        }
        return;
    }
    auto uri = str.substr(semi + 1);
    if (uri.empty()) {
        current_hyperlink_.clear();
    } else if (isAllowedUriScheme(uri)) {
        current_hyperlink_ = uri;
    } else {
        // Reject URIs with disallowed schemes (javascript:, data:, etc.)
        current_hyperlink_.clear();
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
        // Only allow if clipboard write permission has been explicitly granted
        if (!clipboard_write_allowed_) return;
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

    int absolute_row = static_cast<int>(scrollback_ring_.size()) + cursor_.row;

    switch (str[0]) {
    case 'A':
        prompt_state_ = PromptState::Prompt;
        prompt_rows_.push_back(absolute_row);
        prompt_markers_.push_back({absolute_row, PromptState::Prompt});
        // Reset input tracking
        input_start_row_ = -1;
        input_start_col_ = -1;
        // If a command was active, fire finish callback
        if (command_running_ && command_finish_callback_) {
            auto now = std::chrono::steady_clock::now();
            double duration = std::chrono::duration<double>(now - command_start_time_).count();
            if (duration >= static_cast<double>(notify_after_seconds_)) {
                command_finish_callback_(duration);
            }
        }
        command_running_ = false;
        break;
    case 'B':
        prompt_state_ = PromptState::Input;
        prompt_markers_.push_back({absolute_row, PromptState::Input});
        input_start_row_ = static_cast<int>(scrollback_ring_.size()) + cursor_.row;
        input_start_col_ = cursor_.col;
        break;
    case 'C': {
        if (prompt_state_ == PromptState::Input && onCommandCapture_) {
            std::string cmd = currentInputText();
            if (!cmd.empty()) {
                onCommandCapture_(cmd);
            }
        }
        prompt_state_ = PromptState::Output;
        prompt_markers_.push_back({absolute_row, PromptState::Output});
        // Record command start time for completion notifications
        command_start_time_ = std::chrono::steady_clock::now();
        command_running_ = true;
        break;
    }
    case 'D': {
        prompt_state_ = PromptState::None;
        prompt_markers_.push_back({absolute_row, PromptState::None});
        // Compute command duration and fire callback if threshold exceeded
        if (command_running_ && command_finish_callback_) {
            auto now = std::chrono::steady_clock::now();
            double duration = std::chrono::duration<double>(now - command_start_time_).count();
            if (duration >= static_cast<double>(notify_after_seconds_)) {
                command_finish_callback_(duration);
            }
        }
        command_running_ = false;
        break;
    }
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

// --- Prompt navigation methods ---

int Screen::previousPromptRow(int from_row) const {
    // from_row is an absolute row (scrollback + visible).
    // Search prompt_rows_ for the nearest row strictly above from_row.
    int best = -1;
    for (int r : prompt_rows_) {
        if (r < from_row) {
            best = r;
        }
    }
    return best;
}

int Screen::nextPromptRow(int from_row) const {
    // Search prompt_rows_ for the nearest row strictly below from_row.
    for (int r : prompt_rows_) {
        if (r > from_row) {
            return r;
        }
    }
    return -1;
}

std::pair<int,int> Screen::outputRegionAt(int row) const {
    // Find the output region containing or nearest to the given absolute row.
    // Output region: from a 'C' marker to the next 'A' marker (or end of content).
    int output_start = -1;
    int output_end = -1;

    for (size_t i = 0; i < prompt_markers_.size(); ++i) {
        if (prompt_markers_[i].type == PromptState::Output &&
            prompt_markers_[i].absolute_row <= row) {
            output_start = prompt_markers_[i].absolute_row;
            // Find the end: next A marker or end of content
            output_end = static_cast<int>(scrollback_ring_.size()) + rows_;
            for (size_t j = i + 1; j < prompt_markers_.size(); ++j) {
                if (prompt_markers_[j].type == PromptState::Prompt) {
                    output_end = prompt_markers_[j].absolute_row;
                    break;
                }
            }
        }
    }

    if (output_start >= 0 && row < output_end) {
        return {output_start, output_end};
    }
    return {-1, -1};
}

// --- OSC 1337: iTerm2 inline image protocol ---
void Screen::handleOscItermImage(const std::string& str) {
    // Format: File=[params]:[base64_data]
    // Only handle "File=" prefix
    const std::string prefix = "File=";
    if (str.size() < prefix.size() ||
        str.compare(0, prefix.size(), prefix) != 0) {
        return;
    }

    std::string after_file = str.substr(prefix.size());

    ITermImageParams params;
    std::string base64_payload;
    if (!parseITermImageOsc(after_file, params, base64_payload)) {
        return;
    }

    // Only display inline images (inline=1)
    if (!params.inline_display) {
        return;
    }

    // Decode base64 payload
    auto raw_data = iTermBase64Decode(base64_payload);
    if (raw_data.empty()) {
        return;
    }

    // Decode image (PNG, JPEG, GIF, BMP) into RGBA pixels
    int img_width = 0, img_height = 0;
    std::vector<uint8_t> rgba_pixels;
    if (!decodeImageData(raw_data, img_width, img_height, rgba_pixels)) {
        return;
    }

    // Calculate display cell dimensions
    int display_cols = 0, display_rows = 0;
    calculateDisplayCells(params,
                          cell_width_px_, cell_height_px_,
                          cols_, rows_,
                          img_width, img_height,
                          display_cols, display_rows);

    // Store the image using the Kitty graphics infrastructure.
    // This reuses the existing image rendering pipeline.
    KittyImage image;
    image.id = 0; // Will be assigned by the manager
    image.width = img_width;
    image.height = img_height;
    image.format = 32; // RGBA
    image.data = std::move(rgba_pixels);
    image.complete = true;

    uint32_t image_id = kitty_graphics_.addImage(std::move(image));

    // Create a placement at the current cursor position
    KittyPlacement placement;
    placement.image_id = image_id;
    placement.placement_id = 0;
    placement.col = cursor_.col;
    placement.absolute_row = absoluteRowMonotonic();
    placement.cols = display_cols;
    placement.rows = display_rows;

    kitty_graphics_.addPlacement(placement);

    // Move cursor past the image area (to the line after the image)
    cursor_.row += display_rows;
    if (cursor_.row >= rows_) {
        // Scroll as needed
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

// --- OSC 7770: BreadTerminal hook event ---
void Screen::handleOscHookEvent(const std::string& str) {
    if (!osc_hook_callback_ || str.empty()) return;
    if (str.front() != '{') return;
    osc_hook_callback_(str);
}

} // namespace termcore
