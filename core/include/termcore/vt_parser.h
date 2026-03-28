#ifndef TERMCORE_VT_PARSER_H
#define TERMCORE_VT_PARSER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

class TerminalInspector;

/// A single CSI/DCS parameter with optional colon-separated sub-parameters.
struct VtParam {
    int value = -1;              ///< Main parameter value (-1 = default/omitted)
    std::vector<int> sub;        ///< Colon-separated sub-parameters

    /// Check if this param has sub-parameters.
    bool hasSub() const { return !sub.empty(); }

    /// Get sub-parameter at index, or default value.
    int subOr(size_t idx, int def) const {
        if (idx < sub.size() && sub[idx] >= 0) return sub[idx];
        return def;
    }
};

/// Handler interface for VT parser events.
class VtParserHandler {
public:
    virtual ~VtParserHandler() = default;

    /// Called for printable characters (full Unicode codepoints).
    virtual void onPrint(char32_t codepoint) = 0;

    /// Called for bulk printable ASCII (0x20-0x7E) from SIMD fast-path.
    /// Default implementation delegates to onPrint() per character.
    virtual void onPrintAscii(const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i)
            onPrint(static_cast<char32_t>(data[i]));
    }

    /// Called for C0/C1 control codes (BEL, BS, HT, LF, CR, etc.).
    virtual void onExecute(uint8_t byte) = 0;

    /// Called when a CSI sequence is complete.
    virtual void onCsiDispatch(char32_t final_char,
                               const std::vector<VtParam>& params,
                               const std::string& intermediates) = 0;

    /// Called when an ESC sequence is complete.
    virtual void onEscDispatch(char32_t final_char,
                               const std::string& intermediates) = 0;

    /// Called when an OSC sequence is complete.
    virtual void onOscDispatch(int osc_number,
                               const std::string& osc_string) = 0;

    /// Called when a DCS sequence is complete (stub).
    virtual void onDcsDispatch(char32_t final_char,
                               const std::vector<VtParam>& params,
                               const std::string& intermediates,
                               const std::string& data) {
        (void)final_char;
        (void)params;
        (void)intermediates;
        (void)data;
    }

    /// Called when an APC sequence is complete.
    /// data contains everything between ESC _ and ST.
    virtual void onApcDispatch(const std::string& data) {
        (void)data;
    }
};

/// Parser states based on the Paul Faint Williams VT parser state machine.
enum class VtParserState {
    Ground,
    Escape,
    EscapeIntermediate,
    CsiEntry,
    CsiParam,
    CsiIntermediate,
    CsiIgnore,
    OscString,
    DcsEntry,
    DcsParam,
    DcsIntermediate,
    DcsPassthrough,
    DcsIgnore,
    SosPmApcString,
    Utf8Collect,
};

/// VT100/VT220/xterm escape-sequence parser (state machine).
class VtParser {
public:
    explicit VtParser(VtParserHandler& handler);
    ~VtParser() = default;

    /// Feed a chunk of bytes to the parser.
    void feed(const char* data, size_t len);

    /// Reset the parser to Ground state.
    void reset();

    /// Set an optional inspector for logging parsed sequences.
    void setInspector(TerminalInspector* inspector);

private:
    void processByte(uint8_t byte);

    // State handlers
    void handleGround(uint8_t byte);
    void handleEscape(uint8_t byte);
    void handleEscapeIntermediate(uint8_t byte);
    void handleCsiEntry(uint8_t byte);
    void handleCsiParam(uint8_t byte);
    void handleCsiIntermediate(uint8_t byte);
    void handleCsiIgnore(uint8_t byte);
    void handleOscString(uint8_t byte);
    void handleDcsEntry(uint8_t byte);
    void handleDcsParam(uint8_t byte);
    void handleDcsIntermediate(uint8_t byte);
    void handleDcsPassthrough(uint8_t byte);
    void handleSosPmApcString(uint8_t byte);
    void handleUtf8Collect(uint8_t byte);

    // Helpers
    void clear();
    void csiDispatch(uint8_t byte);
    void escDispatch(uint8_t byte);
    void oscDispatch();
    void collectParam(uint8_t byte);
    void collectIntermediate(uint8_t byte);
    bool isC0(uint8_t byte) const;
    void executeC0(uint8_t byte);
    void beginUtf8(uint8_t byte);

    VtParserHandler& handler_;
    TerminalInspector* inspector_ = nullptr;
    VtParserState state_ = VtParserState::Ground;

    // CSI/ESC parameter collection
    std::vector<VtParam> params_;
    std::string intermediates_;
    int current_param_ = -1;
    bool param_started_ = false;
    bool pending_param_ = false;       ///< Semicolon seen, next param slot open
    bool in_sub_param_ = false;        ///< Currently collecting colon sub-params
    std::vector<int> current_subs_;    ///< Sub-params for current param

    // OSC collection
    int osc_number_ = -1;
    bool osc_number_done_ = false;
    std::string osc_string_;

    // DCS collection
    std::string dcs_data_;
    char32_t dcs_final_char_ = 0;  // The byte that transitioned to DcsPassthrough
    bool dcs_pending_ = false;     // ESC received during DcsPassthrough

    // OSC ST tracking
    bool osc_pending_ = false;     // ESC received during OscString
    int osc_pending_number_ = -1;
    bool osc_pending_number_done_ = false;
    std::string osc_pending_string_;

    // APC collection
    std::string apc_string_;
    bool apc_pending_ = false;  // ESC received during APC
    bool apc_active_ = false;   // Currently in APC (not SOS/PM)

    // UTF-8 collection
    char32_t utf8_codepoint_ = 0;
    int utf8_remaining_ = 0;
    VtParserState utf8_return_state_ = VtParserState::Ground;
};

} // namespace termcore

#endif // TERMCORE_VT_PARSER_H
