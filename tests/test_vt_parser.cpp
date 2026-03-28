#include <gtest/gtest.h>
#include "termcore/vt_parser.h"
#include <string>
#include <vector>
#include <utility>

namespace termcore {
namespace {

/// Test handler that records all parser events.
class MockHandler : public VtParserHandler {
public:
    struct PrintEvent {
        char32_t codepoint;
    };
    struct ExecuteEvent {
        uint8_t byte;
    };
    struct CsiEvent {
        char32_t final_char;
        std::vector<VtParam> params;
        std::string intermediates;
    };
    struct EscEvent {
        char32_t final_char;
        std::string intermediates;
    };
    struct OscEvent {
        int osc_number;
        std::string osc_string;
    };

    struct DcsEvent {
        char32_t final_char;
        std::vector<VtParam> params;
        std::string intermediates;
        std::string data;
    };
    struct ApcEvent {
        std::string data;
    };

    std::vector<PrintEvent> prints;
    std::vector<ExecuteEvent> executes;
    std::vector<CsiEvent> csis;
    std::vector<EscEvent> escs;
    std::vector<OscEvent> oscs;
    std::vector<DcsEvent> dcss;
    std::vector<ApcEvent> apcs;

    void onPrint(char32_t codepoint) override {
        prints.push_back({codepoint});
    }
    void onExecute(uint8_t byte) override {
        executes.push_back({byte});
    }
    void onCsiDispatch(char32_t final_char,
                       const std::vector<VtParam>& params,
                       const std::string& intermediates) override {
        csis.push_back({final_char, params, intermediates});
    }
    void onEscDispatch(char32_t final_char,
                       const std::string& intermediates) override {
        escs.push_back({final_char, intermediates});
    }
    void onOscDispatch(int osc_number,
                       const std::string& osc_string) override {
        oscs.push_back({osc_number, osc_string});
    }
    void onDcsDispatch(char32_t final_char,
                       const std::vector<VtParam>& params,
                       const std::string& intermediates,
                       const std::string& data) override {
        dcss.push_back({final_char, params, intermediates, data});
    }
    void onApcDispatch(const std::string& data) override {
        apcs.push_back({data});
    }
};

class VtParserTest : public ::testing::Test {
protected:
    MockHandler handler;
    VtParser parser{handler};

    void feed(const std::string& data) {
        parser.feed(data.data(), data.size());
    }
};

// Test 1: Plain ASCII text -> onPrint for each character
TEST_F(VtParserTest, PlainAsciiText) {
    feed("Hello");
    ASSERT_EQ(handler.prints.size(), 5u);
    EXPECT_EQ(handler.prints[0].codepoint, U'H');
    EXPECT_EQ(handler.prints[1].codepoint, U'e');
    EXPECT_EQ(handler.prints[2].codepoint, U'l');
    EXPECT_EQ(handler.prints[3].codepoint, U'l');
    EXPECT_EQ(handler.prints[4].codepoint, U'o');
    EXPECT_TRUE(handler.executes.empty());
    EXPECT_TRUE(handler.csis.empty());
}

// Test 2: ESC [ 2 J -> onCsiDispatch with final='J', params={2}
TEST_F(VtParserTest, CsiEraseDisplay) {
    feed("\x1B[2J");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'J');
    ASSERT_EQ(handler.csis[0].params.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 2);
    EXPECT_TRUE(handler.csis[0].intermediates.empty());
}

// Test 3: ESC [ 1 ; 3 1 m -> onCsiDispatch with final='m', params={1, 31}
TEST_F(VtParserTest, CsiSgrBoldRed) {
    feed("\x1B[1;31m");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'm');
    ASSERT_EQ(handler.csis[0].params.size(), 2u);
    EXPECT_EQ(handler.csis[0].params[0].value, 1);
    EXPECT_EQ(handler.csis[0].params[1].value, 31);
}

// Test 4: ESC [ m -> onCsiDispatch with final='m', params={}
TEST_F(VtParserTest, CsiSgrDefault) {
    feed("\x1B[m");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'm');
    EXPECT_TRUE(handler.csis[0].params.empty());
}

// Test 5: ESC ] 0 ; title BEL -> onOscDispatch
TEST_F(VtParserTest, OscSetTitle) {
    feed("\x1B]0;title\x07");
    ASSERT_EQ(handler.oscs.size(), 1u);
    EXPECT_EQ(handler.oscs[0].osc_number, 0);
    EXPECT_EQ(handler.oscs[0].osc_string, "title");
}

// Test 6: CR, LF, BS, BEL -> onExecute
TEST_F(VtParserTest, ControlCodes) {
    feed("\r\n\x08\x07");
    ASSERT_EQ(handler.executes.size(), 4u);
    EXPECT_EQ(handler.executes[0].byte, '\r');
    EXPECT_EQ(handler.executes[1].byte, '\n');
    EXPECT_EQ(handler.executes[2].byte, 0x08);  // BS
    EXPECT_EQ(handler.executes[3].byte, 0x07);  // BEL
}

// Test 7: UTF-8 multi-byte character (Korean: 한)
TEST_F(VtParserTest, Utf8Korean) {
    // U+D55C (한) = 0xED 0x95 0x9C in UTF-8
    feed("\xED\x95\x9C");
    ASSERT_EQ(handler.prints.size(), 1u);
    EXPECT_EQ(handler.prints[0].codepoint, U'\uD55C');
}

// Test 7b: UTF-8 4-byte character (emoji: U+1F600)
TEST_F(VtParserTest, Utf8Emoji) {
    feed("\xF0\x9F\x98\x80");
    ASSERT_EQ(handler.prints.size(), 1u);
    EXPECT_EQ(handler.prints[0].codepoint, U'\U0001F600');
}

// Test 8: Mixed content - text + escape sequences interleaved
TEST_F(VtParserTest, MixedContent) {
    feed("AB\x1B[1mCD\x1B[0mEF");
    // Prints: A, B, C, D, E, F
    ASSERT_EQ(handler.prints.size(), 6u);
    EXPECT_EQ(handler.prints[0].codepoint, U'A');
    EXPECT_EQ(handler.prints[1].codepoint, U'B');
    EXPECT_EQ(handler.prints[2].codepoint, U'C');
    EXPECT_EQ(handler.prints[3].codepoint, U'D');
    EXPECT_EQ(handler.prints[4].codepoint, U'E');
    EXPECT_EQ(handler.prints[5].codepoint, U'F');
    // CSI: bold on, reset
    ASSERT_EQ(handler.csis.size(), 2u);
    EXPECT_EQ(handler.csis[0].final_char, U'm');
    EXPECT_EQ(handler.csis[0].params[0].value, 1);
    EXPECT_EQ(handler.csis[1].final_char, U'm');
    EXPECT_EQ(handler.csis[1].params[0].value, 0);
}

// Test 9: Partial/split sequences - ESC in one feed, [ in next
TEST_F(VtParserTest, SplitSequence) {
    parser.feed("\x1B", 1);       // ESC alone
    parser.feed("[", 1);          // [
    parser.feed("3", 1);          // param
    parser.feed("1", 1);          // param
    parser.feed("m", 1);          // final
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'm');
    ASSERT_EQ(handler.csis[0].params.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 31);
}

// Test 10: ESC D -> onEscDispatch
TEST_F(VtParserTest, EscSequenceIndexDown) {
    feed("\x1B" "D");
    ASSERT_EQ(handler.escs.size(), 1u);
    EXPECT_EQ(handler.escs[0].final_char, U'D');
    EXPECT_TRUE(handler.escs[0].intermediates.empty());
}

// Additional: ESC with intermediate bytes (e.g., ESC ( B for charset)
TEST_F(VtParserTest, EscWithIntermediate) {
    feed("\x1B(B");
    ASSERT_EQ(handler.escs.size(), 1u);
    EXPECT_EQ(handler.escs[0].final_char, U'B');
    EXPECT_EQ(handler.escs[0].intermediates, "(");
}

// Additional: CSI with multiple default params (ESC [ ; ; H)
TEST_F(VtParserTest, CsiDefaultParams) {
    feed("\x1B[;;H");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'H');
    ASSERT_EQ(handler.csis[0].params.size(), 2u);
    EXPECT_EQ(handler.csis[0].params[0].value, -1);  // default
    EXPECT_EQ(handler.csis[0].params[1].value, -1);  // default
}

// Additional: CSI with private marker (ESC [ ? 25 h - show cursor)
TEST_F(VtParserTest, CsiPrivateMode) {
    feed("\x1B[?25h");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'h');
    ASSERT_EQ(handler.csis[0].params.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 25);
    EXPECT_EQ(handler.csis[0].intermediates, "?");
}

// Additional: HT (tab) in middle of text
TEST_F(VtParserTest, TabInText) {
    feed("A\tB");
    ASSERT_EQ(handler.prints.size(), 2u);
    EXPECT_EQ(handler.prints[0].codepoint, U'A');
    EXPECT_EQ(handler.prints[1].codepoint, U'B');
    ASSERT_EQ(handler.executes.size(), 1u);
    EXPECT_EQ(handler.executes[0].byte, 0x09);  // HT
}

// Additional: OSC with longer string
TEST_F(VtParserTest, OscLongerTitle) {
    feed("\x1B]2;My Terminal Title\x07");
    ASSERT_EQ(handler.oscs.size(), 1u);
    EXPECT_EQ(handler.oscs[0].osc_number, 2);
    EXPECT_EQ(handler.oscs[0].osc_string, "My Terminal Title");
}

// Additional: C0 controls within CSI sequence
TEST_F(VtParserTest, C0WithinCsi) {
    // LF in the middle of a CSI sequence should be executed
    feed("\x1B[1\n;31m");
    ASSERT_EQ(handler.executes.size(), 1u);
    EXPECT_EQ(handler.executes[0].byte, '\n');
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 1);
    EXPECT_EQ(handler.csis[0].params[1].value, 31);
}

// Additional: Multiple UTF-8 characters in sequence
TEST_F(VtParserTest, Utf8MultipleChars) {
    // "han-geul" = U+D55C U+AE00
    feed("\xED\x95\x9C\xEA\xB8\x80");
    ASSERT_EQ(handler.prints.size(), 2u);
    EXPECT_EQ(handler.prints[0].codepoint, U'\uD55C');
    EXPECT_EQ(handler.prints[1].codepoint, U'\uAE00');
}

// Additional: CAN (0x18) cancels escape sequence
TEST_F(VtParserTest, CanCancelsSequence) {
    feed("\x1B[\x18""A");
    // CAN should cancel the CSI and execute, then 'A' is printed
    ASSERT_EQ(handler.executes.size(), 1u);
    EXPECT_EQ(handler.executes[0].byte, 0x18);
    ASSERT_EQ(handler.prints.size(), 1u);
    EXPECT_EQ(handler.prints[0].codepoint, U'A');
    EXPECT_TRUE(handler.csis.empty());
}

// --- Sub-parameter parsing tests ---

// Colon-separated sub-parameters: ESC[4:3m -> param 4 with sub=[3]
TEST_F(VtParserTest, ColonSubParam) {
    feed("\x1B[4:3m");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'm');
    ASSERT_EQ(handler.csis[0].params.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 4);
    ASSERT_EQ(handler.csis[0].params[0].sub.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].sub[0], 3);
}

// Multiple colons: ESC[58:2::255:0:0m
TEST_F(VtParserTest, ColonMultiSubParam) {
    feed("\x1B[58:2::255:0:0m");
    ASSERT_EQ(handler.csis.size(), 1u);
    ASSERT_EQ(handler.csis[0].params.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 58);
    // sub = [2, -1, 255, 0, 0]
    ASSERT_EQ(handler.csis[0].params[0].sub.size(), 5u);
    EXPECT_EQ(handler.csis[0].params[0].sub[0], 2);
    EXPECT_EQ(handler.csis[0].params[0].sub[1], -1);  // empty between ::
    EXPECT_EQ(handler.csis[0].params[0].sub[2], 255);
    EXPECT_EQ(handler.csis[0].params[0].sub[3], 0);
    EXPECT_EQ(handler.csis[0].params[0].sub[4], 0);
}

// Mixed semicolon and colon: ESC[4:3;38:2::128:0:255m
TEST_F(VtParserTest, MixedSemicolonColon) {
    feed("\x1B[4:3;38:2::128:0:255m");
    ASSERT_EQ(handler.csis.size(), 1u);
    ASSERT_EQ(handler.csis[0].params.size(), 2u);
    // First param: 4:3
    EXPECT_EQ(handler.csis[0].params[0].value, 4);
    ASSERT_EQ(handler.csis[0].params[0].sub.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].sub[0], 3);
    // Second param: 38:2::128:0:255
    EXPECT_EQ(handler.csis[0].params[1].value, 38);
    ASSERT_EQ(handler.csis[0].params[1].sub.size(), 5u);
    EXPECT_EQ(handler.csis[0].params[1].sub[0], 2);
    EXPECT_EQ(handler.csis[0].params[1].sub[1], -1);
    EXPECT_EQ(handler.csis[0].params[1].sub[2], 128);
    EXPECT_EQ(handler.csis[0].params[1].sub[3], 0);
    EXPECT_EQ(handler.csis[0].params[1].sub[4], 255);
}

// Simple param without sub-params should have empty sub vector
TEST_F(VtParserTest, NoSubParams) {
    feed("\x1B[38;2;128;0;255m");
    ASSERT_EQ(handler.csis.size(), 1u);
    ASSERT_EQ(handler.csis[0].params.size(), 5u);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_TRUE(handler.csis[0].params[i].sub.empty());
    }
    EXPECT_EQ(handler.csis[0].params[0].value, 38);
    EXPECT_EQ(handler.csis[0].params[1].value, 2);
    EXPECT_EQ(handler.csis[0].params[2].value, 128);
    EXPECT_EQ(handler.csis[0].params[3].value, 0);
    EXPECT_EQ(handler.csis[0].params[4].value, 255);
}

// --- DCS test cases ---

// Basic DCS sequence terminated by ESC backslash (ST)
TEST_F(VtParserTest, DcsSimple) {
    // ESC P q ESC backslash  (DCS with final char 'q', no params, no data)
    feed("\x1BPq\x1B\\");
    ASSERT_EQ(handler.dcss.size(), 1u);
    EXPECT_EQ(handler.dcss[0].final_char, U'q');
    EXPECT_TRUE(handler.dcss[0].params.empty());
    EXPECT_TRUE(handler.dcss[0].intermediates.empty());
    EXPECT_TRUE(handler.dcss[0].data.empty());
}

// DCS with numeric parameters
TEST_F(VtParserTest, DcsWithParams) {
    // ESC P 1 ; 2 q ESC backslash
    feed("\x1BP1;2q\x1B\\");
    ASSERT_EQ(handler.dcss.size(), 1u);
    EXPECT_EQ(handler.dcss[0].final_char, U'q');
    ASSERT_EQ(handler.dcss[0].params.size(), 2u);
    EXPECT_EQ(handler.dcss[0].params[0].value, 1);
    EXPECT_EQ(handler.dcss[0].params[1].value, 2);
}

// DCS with passthrough data payload
TEST_F(VtParserTest, DcsPassthroughData) {
    // ESC P q <data> ESC backslash
    feed("\x1BPqHello World\x1B\\");
    ASSERT_EQ(handler.dcss.size(), 1u);
    EXPECT_EQ(handler.dcss[0].final_char, U'q');
    EXPECT_EQ(handler.dcss[0].data, "Hello World");
}

// DCS terminated by ESC backslash (explicit ST test)
TEST_F(VtParserTest, DcsWithEscStTerminator) {
    // Sixel-like: ESC P 0 ; 1 q #0;2;0;0;0 ESC backslash
    feed("\x1BP0;1q#0;2;0;0;0\x1B\\");
    ASSERT_EQ(handler.dcss.size(), 1u);
    EXPECT_EQ(handler.dcss[0].final_char, U'q');
    ASSERT_EQ(handler.dcss[0].params.size(), 2u);
    EXPECT_EQ(handler.dcss[0].params[0].value, 0);
    EXPECT_EQ(handler.dcss[0].params[1].value, 1);
    EXPECT_EQ(handler.dcss[0].data, "#0;2;0;0;0");
}

// --- APC test cases ---

// Basic APC sequence
TEST_F(VtParserTest, ApcSimple) {
    // ESC _ hello ESC backslash
    feed("\x1B_hello\x1B\\");
    ASSERT_EQ(handler.apcs.size(), 1u);
    EXPECT_EQ(handler.apcs[0].data, "hello");
}

// APC with payload data
TEST_F(VtParserTest, ApcWithData) {
    // ESC _ key=value ESC backslash
    feed("\x1B_key=value\x1B\\");
    ASSERT_EQ(handler.apcs.size(), 1u);
    EXPECT_EQ(handler.apcs[0].data, "key=value");
}

// APC typical for Kitty graphics protocol
TEST_F(VtParserTest, ApcKittyGraphics) {
    // Kitty graphics: ESC _ G <payload> ESC backslash
    feed("\x1B_Ga=T,f=100,s=1,v=1;AAAA\x1B\\");
    ASSERT_EQ(handler.apcs.size(), 1u);
    EXPECT_EQ(handler.apcs[0].data, "Ga=T,f=100,s=1,v=1;AAAA");
}

// --- ST terminator tests ---

// OSC terminated by ESC backslash (not just BEL)
TEST_F(VtParserTest, OscWithEscStTerminator) {
    feed("\x1B]0;title\x1B\\");
    ASSERT_EQ(handler.oscs.size(), 1u);
    EXPECT_EQ(handler.oscs[0].osc_number, 0);
    EXPECT_EQ(handler.oscs[0].osc_string, "title");
}

// --- Boundary/error test cases ---

// Parameter values are clamped at 65535
TEST_F(VtParserTest, ParamClamp65535) {
    feed("\x1B[99999m");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'm');
    ASSERT_EQ(handler.csis[0].params.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 65535);
}

// Invalid UTF-8 continuation byte: parser recovers and processes next byte
TEST_F(VtParserTest, Utf8Invalid) {
    // Start a 2-byte UTF-8 sequence (0xC3) but follow with non-continuation 'A'
    feed("\xC3""A");
    // The invalid continuation should cause the parser to abort UTF-8 and
    // reprocess 'A' as a printable character
    ASSERT_GE(handler.prints.size(), 1u);
    EXPECT_EQ(handler.prints.back().codepoint, U'A');
}

// Incomplete UTF-8 at end of feed: no crash, no spurious output
TEST_F(VtParserTest, Utf8IncompleteAtEof) {
    // Start of a 3-byte UTF-8 sequence with only 1 continuation byte
    feed("\xE2\x9C");
    // Should not crash; the incomplete codepoint should not produce a print
    EXPECT_TRUE(handler.prints.empty());
    // Feeding next valid continuation should complete it
    feed("\xB3");  // completes U+2733 (sparkle)
    ASSERT_EQ(handler.prints.size(), 1u);
    EXPECT_EQ(handler.prints[0].codepoint, U'\u2733');
}

// ESC as last byte: parser is in escape state, no dispatch yet
TEST_F(VtParserTest, EscAloneAtEof) {
    feed("\x1B");
    // No events should have been dispatched
    EXPECT_TRUE(handler.escs.empty());
    EXPECT_TRUE(handler.csis.empty());
    EXPECT_TRUE(handler.oscs.empty());
    // Completing the sequence in a subsequent feed should work
    feed("D");
    ASSERT_EQ(handler.escs.size(), 1u);
    EXPECT_EQ(handler.escs[0].final_char, U'D');
}

// Partial CSI at end of feed: no dispatch until completed
TEST_F(VtParserTest, PartialCsiAtEof) {
    feed("\x1B[31");
    // No CSI dispatched yet
    EXPECT_TRUE(handler.csis.empty());
    // Complete it
    feed("m");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'm');
    ASSERT_EQ(handler.csis[0].params.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 31);
}

// CSI with trailing semicolon: trailing semicolon is ignored (no extra param)
TEST_F(VtParserTest, CsiTrailingSemicolon) {
    feed("\x1B[1;m");
    ASSERT_EQ(handler.csis.size(), 1u);
    EXPECT_EQ(handler.csis[0].final_char, U'm');
    // Trailing semicolons do not produce an additional default parameter,
    // matching the behavior of most terminal emulators.
    ASSERT_EQ(handler.csis[0].params.size(), 1u);
    EXPECT_EQ(handler.csis[0].params[0].value, 1);
}

} // namespace
} // namespace termcore
