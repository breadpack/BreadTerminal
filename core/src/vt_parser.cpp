#include "termcore/vt_parser.h"

namespace termcore {

VtParser::VtParser(VtParserHandler& handler)
    : handler_(handler) {
    params_.reserve(16);
    current_subs_.reserve(4);
    osc_string_.reserve(512);
    dcs_data_.reserve(256);
    intermediates_.reserve(4);
}

void VtParser::feed(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        processByte(static_cast<uint8_t>(data[i]));
    }
}

void VtParser::reset() {
    state_ = VtParserState::Ground;
    clear();
    osc_pending_ = false;
    osc_pending_number_ = -1;
    osc_pending_number_done_ = false;
    osc_pending_string_.clear();
    utf8_codepoint_ = 0;
    utf8_remaining_ = 0;
}

void VtParser::clear() {
    params_.clear();
    intermediates_.clear();
    current_param_ = -1;
    param_started_ = false;
    in_sub_param_ = false;
    current_subs_.clear();
    osc_number_ = -1;
    osc_number_done_ = false;
    osc_string_.clear();
    dcs_data_.clear();
    dcs_final_char_ = 0;
    dcs_pending_ = false;
    // Note: osc_pending_ fields are NOT cleared here; they are managed
    // explicitly in processByte/handleEscape to survive across the
    // OscString -> Escape state transition.
}

bool VtParser::isC0(uint8_t byte) const {
    return byte < 0x20 || byte == 0x7F;
}

void VtParser::executeC0(uint8_t byte) {
    handler_.onExecute(byte);
}

void VtParser::collectParam(uint8_t byte) {
    if (byte == ';') {
        // Semicolon: finish current param (with any sub-params) and start next
        if (in_sub_param_) {
            // Push final sub-param value
            current_subs_.push_back(param_started_ ? current_param_ : -1);
            in_sub_param_ = false;
        }
        VtParam vp;
        if (!current_subs_.empty()) {
            // First sub is the main param value
            vp.value = current_subs_[0];
            vp.sub.assign(current_subs_.begin() + 1, current_subs_.end());
        } else {
            vp.value = param_started_ ? current_param_ : -1;
        }
        params_.push_back(std::move(vp));
        current_param_ = -1;
        param_started_ = false;
        current_subs_.clear();
    } else if (byte == ':') {
        // Colon: start/continue sub-parameter collection
        if (!in_sub_param_) {
            // First colon: the current_param_ becomes the first sub-value (main value)
            current_subs_.push_back(param_started_ ? current_param_ : -1);
            in_sub_param_ = true;
        } else {
            // Subsequent colon: push the current sub-value
            current_subs_.push_back(param_started_ ? current_param_ : -1);
        }
        current_param_ = -1;
        param_started_ = false;
    } else if (byte >= '0' && byte <= '9') {
        if (!param_started_) {
            current_param_ = 0;
            param_started_ = true;
        }
        if (current_param_ > 6553) {
            current_param_ = 65535;
            return;
        }
        current_param_ = current_param_ * 10 + (byte - '0');
        if (current_param_ > 65535) current_param_ = 65535;
    }
}

void VtParser::collectIntermediate(uint8_t byte) {
    intermediates_.push_back(static_cast<char>(byte));
}

void VtParser::csiDispatch(uint8_t byte) {
    // Finalize last param (with any pending sub-params)
    if (param_started_ || in_sub_param_ || !current_subs_.empty()) {
        if (in_sub_param_) {
            current_subs_.push_back(param_started_ ? current_param_ : -1);
            in_sub_param_ = false;
        }
        VtParam vp;
        if (!current_subs_.empty()) {
            vp.value = current_subs_[0];
            vp.sub.assign(current_subs_.begin() + 1, current_subs_.end());
        } else {
            vp.value = param_started_ ? current_param_ : -1;
        }
        params_.push_back(std::move(vp));
        current_subs_.clear();
    }
    handler_.onCsiDispatch(static_cast<char32_t>(byte), params_, intermediates_);
}

void VtParser::escDispatch(uint8_t byte) {
    handler_.onEscDispatch(static_cast<char32_t>(byte), intermediates_);
}

void VtParser::oscDispatch() {
    handler_.onOscDispatch(osc_number_, osc_string_);
}

void VtParser::beginUtf8(uint8_t byte) {
    if ((byte & 0xE0) == 0xC0) {
        utf8_codepoint_ = byte & 0x1F;
        utf8_remaining_ = 1;
    } else if ((byte & 0xF0) == 0xE0) {
        utf8_codepoint_ = byte & 0x0F;
        utf8_remaining_ = 2;
    } else if ((byte & 0xF8) == 0xF0) {
        utf8_codepoint_ = byte & 0x07;
        utf8_remaining_ = 3;
    }
}

void VtParser::processByte(uint8_t byte) {
    // Anywhere transitions: ESC, CAN, SUB always take effect
    if (byte == 0x1B && state_ != VtParserState::Utf8Collect) {
        if (state_ == VtParserState::DcsPassthrough) {
            // In DCS passthrough, ESC might be the start of ST (ESC \).
            // Save the flag and transition to Escape; handleEscape will
            // check dcs_pending_ to dispatch on '\'.
            dcs_pending_ = true;
            state_ = VtParserState::Escape;
            return;
        }
        if (state_ == VtParserState::OscString) {
            // In OSC string, ESC might be the start of ST (ESC \).
            // Save the OSC state so we can dispatch if next byte is '\'.
            osc_pending_ = true;
            osc_pending_number_ = osc_number_;
            osc_pending_number_done_ = osc_number_done_;
            osc_pending_string_ = osc_string_;
            clear();
            state_ = VtParserState::Escape;
            return;
        }
        // ESC
        clear();
        state_ = VtParserState::Escape;
        return;
    }
    if ((byte == 0x18 || byte == 0x1A) && state_ != VtParserState::Ground
        && state_ != VtParserState::Utf8Collect) {
        // CAN, SUB -> execute and go to Ground
        executeC0(byte);
        state_ = VtParserState::Ground;
        return;
    }

    switch (state_) {
        case VtParserState::Ground:           handleGround(byte); break;
        case VtParserState::Escape:           handleEscape(byte); break;
        case VtParserState::EscapeIntermediate: handleEscapeIntermediate(byte); break;
        case VtParserState::CsiEntry:         handleCsiEntry(byte); break;
        case VtParserState::CsiParam:         handleCsiParam(byte); break;
        case VtParserState::CsiIntermediate:  handleCsiIntermediate(byte); break;
        case VtParserState::CsiIgnore:        handleCsiIgnore(byte); break;
        case VtParserState::OscString:        handleOscString(byte); break;
        case VtParserState::DcsEntry:         handleDcsEntry(byte); break;
        case VtParserState::DcsParam:         handleDcsParam(byte); break;
        case VtParserState::DcsIntermediate:  handleDcsIntermediate(byte); break;
        case VtParserState::DcsPassthrough:   handleDcsPassthrough(byte); break;
        case VtParserState::DcsIgnore:        handleSosPmApcString(byte); break;
        case VtParserState::SosPmApcString:   handleSosPmApcString(byte); break;
        case VtParserState::Utf8Collect:      handleUtf8Collect(byte); break;
    }
}

void VtParser::handleGround(uint8_t byte) {
    if (byte < 0x20) {
        // C0 controls
        executeC0(byte);
    } else if (byte == 0x7F) {
        // DEL - ignore in ground
    } else if (byte >= 0x80 && byte <= 0x9F) {
        // C1 controls (8-bit)
        if (byte == 0x9B) {
            // CSI
            clear();
            state_ = VtParserState::CsiEntry;
        } else if (byte == 0x9D) {
            // OSC
            clear();
            state_ = VtParserState::OscString;
        } else if (byte == 0x90) {
            // DCS
            clear();
            state_ = VtParserState::DcsEntry;
        } else {
            executeC0(byte);
        }
    } else if (byte >= 0xC0) {
        // Start of UTF-8 multi-byte sequence
        utf8_return_state_ = VtParserState::Ground;
        beginUtf8(byte);
        state_ = VtParserState::Utf8Collect;
    } else if (byte >= 0x80 && byte < 0xC0) {
        // Stray continuation byte, ignore or print replacement
    } else {
        // Printable ASCII
        handler_.onPrint(static_cast<char32_t>(byte));
    }
}

void VtParser::handleEscape(uint8_t byte) {
    // If an OSC string was in progress, ESC was the start of ST (ESC \).
    if (osc_pending_) {
        if (byte == '\\') {
            // ST received: dispatch the saved OSC sequence.
            osc_number_ = osc_pending_number_;
            osc_number_done_ = osc_pending_number_done_;
            osc_string_ = std::move(osc_pending_string_);
            osc_pending_ = false;
            oscDispatch();
            clear();
            state_ = VtParserState::Ground;
            return;
        }
        // Not ST — the ESC was part of something else. Discard the OSC.
        osc_pending_ = false;
        osc_pending_string_.clear();
        // Fall through to normal escape handling for this byte.
    }

    // If a DCS passthrough was in progress, ESC was the start of ST (ESC \).
    if (dcs_pending_) {
        dcs_pending_ = false;
        if (byte == '\\') {
            // ST received: dispatch the collected DCS sequence.
            if (param_started_ || in_sub_param_ || !current_subs_.empty()) {
                if (in_sub_param_) {
                    current_subs_.push_back(param_started_ ? current_param_ : -1);
                    in_sub_param_ = false;
                }
                VtParam vp;
                if (!current_subs_.empty()) {
                    vp.value = current_subs_[0];
                    vp.sub.assign(current_subs_.begin() + 1, current_subs_.end());
                } else {
                    vp.value = current_param_;
                }
                params_.push_back(std::move(vp));
                current_subs_.clear();
                param_started_ = false;
            }
            handler_.onDcsDispatch(dcs_final_char_, params_, intermediates_, dcs_data_);
            clear();
            state_ = VtParserState::Ground;
            return;
        }
        // Not ST — the ESC was part of something else. Discard the DCS.
        clear();
        // Fall through to normal escape handling for this byte.
    }

    if (byte < 0x20) {
        // C0 in escape - execute
        executeC0(byte);
        return;
    }
    if (byte == 0x7F) {
        // DEL - ignore
        return;
    }
    if (byte == '[') {
        // CSI
        clear();
        state_ = VtParserState::CsiEntry;
    } else if (byte == ']') {
        // OSC
        clear();
        state_ = VtParserState::OscString;
    } else if (byte == 'P') {
        // DCS
        clear();
        state_ = VtParserState::DcsEntry;
    } else if (byte == 'X' || byte == '^' || byte == '_') {
        // SOS, PM, APC
        state_ = VtParserState::SosPmApcString;
    } else if (byte >= 0x20 && byte <= 0x2F) {
        // Intermediate byte
        clear();
        collectIntermediate(byte);
        state_ = VtParserState::EscapeIntermediate;
    } else if (byte >= 0x30 && byte <= 0x7E) {
        // Final byte -> dispatch ESC sequence
        escDispatch(byte);
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleEscapeIntermediate(uint8_t byte) {
    if (byte < 0x20) {
        executeC0(byte);
        return;
    }
    if (byte == 0x7F) return;
    if (byte >= 0x20 && byte <= 0x2F) {
        collectIntermediate(byte);
    } else if (byte >= 0x30 && byte <= 0x7E) {
        escDispatch(byte);
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleCsiEntry(uint8_t byte) {
    if (byte < 0x20) {
        executeC0(byte);
        return;
    }
    if (byte == 0x7F) return;
    if (byte >= '0' && byte <= '9') {
        collectParam(byte);
        state_ = VtParserState::CsiParam;
    } else if (byte == ';') {
        collectParam(byte);
        state_ = VtParserState::CsiParam;
    } else if (byte >= 0x3C && byte <= 0x3F) {
        // Private marker: < = > ?
        collectIntermediate(byte);
        state_ = VtParserState::CsiParam;
    } else if (byte >= 0x20 && byte <= 0x2F) {
        collectIntermediate(byte);
        state_ = VtParserState::CsiIntermediate;
    } else if (byte >= 0x40 && byte <= 0x7E) {
        // Final byte with no params
        csiDispatch(byte);
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleCsiParam(uint8_t byte) {
    if (byte < 0x20) {
        executeC0(byte);
        return;
    }
    if (byte == 0x7F) return;
    if ((byte >= '0' && byte <= '9') || byte == ';' || byte == ':') {
        collectParam(byte);
    } else if (byte >= 0x3C && byte <= 0x3F) {
        // Invalid: parameter marker after params started -> ignore
        state_ = VtParserState::CsiIgnore;
    } else if (byte >= 0x20 && byte <= 0x2F) {
        collectIntermediate(byte);
        state_ = VtParserState::CsiIntermediate;
    } else if (byte >= 0x40 && byte <= 0x7E) {
        csiDispatch(byte);
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleCsiIntermediate(uint8_t byte) {
    if (byte < 0x20) {
        executeC0(byte);
        return;
    }
    if (byte == 0x7F) return;
    if (byte >= 0x20 && byte <= 0x2F) {
        collectIntermediate(byte);
    } else if (byte >= 0x30 && byte <= 0x3F) {
        // Invalid
        state_ = VtParserState::CsiIgnore;
    } else if (byte >= 0x40 && byte <= 0x7E) {
        csiDispatch(byte);
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleCsiIgnore(uint8_t byte) {
    if (byte < 0x20) {
        executeC0(byte);
        return;
    }
    if (byte >= 0x40 && byte <= 0x7E) {
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleOscString(uint8_t byte) {
    // BEL terminates OSC
    if (byte == 0x07) {
        oscDispatch();
        state_ = VtParserState::Ground;
        return;
    }
    // ST (ESC \) is handled by the ESC anywhere transition + ground
    // But we also need to handle the 0x9C (8-bit ST)
    if (byte == 0x9C) {
        oscDispatch();
        state_ = VtParserState::Ground;
        return;
    }
    // Collect OSC data
    if (!osc_number_done_) {
        if (byte >= '0' && byte <= '9') {
            if (osc_number_ < 0) osc_number_ = 0;
            osc_number_ = osc_number_ * 10 + (byte - '0');
        } else if (byte == ';') {
            osc_number_done_ = true;
        } else {
            // Non-numeric before semicolon; treat rest as string
            osc_number_done_ = true;
            osc_string_.push_back(static_cast<char>(byte));
        }
    } else {
        if (byte >= 0x20 || byte == 0x09) {
            osc_string_.push_back(static_cast<char>(byte));
        }
    }
    // Guard against unbounded OSC buffer growth (1 MB cap)
    if (osc_string_.size() > 1048576) {
        osc_string_.clear();
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleDcsEntry(uint8_t byte) {
    if (byte < 0x20) {
        // Ignore C0 in DCS entry
        return;
    }
    if (byte == 0x7F) return;
    if (byte >= '0' && byte <= '9') {
        collectParam(byte);
        state_ = VtParserState::DcsParam;
    } else if (byte == ';') {
        collectParam(byte);
        state_ = VtParserState::DcsParam;
    } else if (byte >= 0x3C && byte <= 0x3F) {
        collectIntermediate(byte);
        state_ = VtParserState::DcsParam;
    } else if (byte >= 0x20 && byte <= 0x2F) {
        collectIntermediate(byte);
        state_ = VtParserState::DcsIntermediate;
    } else if (byte >= 0x40 && byte <= 0x7E) {
        dcs_final_char_ = static_cast<char32_t>(byte);
        state_ = VtParserState::DcsPassthrough;
    }
}

void VtParser::handleDcsParam(uint8_t byte) {
    if (byte < 0x20) return;
    if (byte == 0x7F) return;
    if ((byte >= '0' && byte <= '9') || byte == ';') {
        collectParam(byte);
    } else if (byte >= 0x20 && byte <= 0x2F) {
        collectIntermediate(byte);
        state_ = VtParserState::DcsIntermediate;
    } else if (byte >= 0x40 && byte <= 0x7E) {
        dcs_final_char_ = static_cast<char32_t>(byte);
        state_ = VtParserState::DcsPassthrough;
    } else if (byte >= 0x3C && byte <= 0x3F) {
        state_ = VtParserState::DcsIgnore;
    }
}

void VtParser::handleDcsIntermediate(uint8_t byte) {
    if (byte < 0x20) return;
    if (byte == 0x7F) return;
    if (byte >= 0x20 && byte <= 0x2F) {
        collectIntermediate(byte);
    } else if (byte >= 0x40 && byte <= 0x7E) {
        dcs_final_char_ = static_cast<char32_t>(byte);
        state_ = VtParserState::DcsPassthrough;
    } else if (byte >= 0x30 && byte <= 0x3F) {
        state_ = VtParserState::DcsIgnore;
    }
}

void VtParser::handleDcsPassthrough(uint8_t byte) {
    // ST (0x9C) or ESC \ terminates
    if (byte == 0x9C) {
        // Push last param
        if (param_started_ || in_sub_param_ || !current_subs_.empty()) {
            if (in_sub_param_) {
                current_subs_.push_back(param_started_ ? current_param_ : -1);
                in_sub_param_ = false;
            }
            VtParam vp;
            if (!current_subs_.empty()) {
                vp.value = current_subs_[0];
                vp.sub.assign(current_subs_.begin() + 1, current_subs_.end());
            } else {
                vp.value = current_param_;
            }
            params_.push_back(std::move(vp));
            current_subs_.clear();
            param_started_ = false;
        }
        handler_.onDcsDispatch(dcs_final_char_, params_, intermediates_, dcs_data_);
        state_ = VtParserState::Ground;
        return;
    }
    if (byte < 0x20 && byte != 0x1B) {
        // Ignore most C0 in passthrough
        return;
    }
    if (byte == 0x7F) return;
    dcs_data_.push_back(static_cast<char>(byte));
    // Guard against unbounded DCS buffer growth (1 MB cap)
    if (dcs_data_.size() > 1048576) {
        dcs_data_.clear();
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleSosPmApcString(uint8_t byte) {
    // Just consume until ST (ESC \ handled by ESC anywhere, 0x9C here)
    if (byte == 0x9C) {
        state_ = VtParserState::Ground;
    }
}

void VtParser::handleUtf8Collect(uint8_t byte) {
    if ((byte & 0xC0) != 0x80) {
        // Invalid continuation byte - abort UTF-8, reprocess this byte
        state_ = utf8_return_state_;
        processByte(byte);
        return;
    }
    utf8_codepoint_ = (utf8_codepoint_ << 6) | (byte & 0x3F);
    utf8_remaining_--;
    if (utf8_remaining_ == 0) {
        state_ = utf8_return_state_;
        handler_.onPrint(utf8_codepoint_);
    }
}

} // namespace termcore
