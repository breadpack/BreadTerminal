#include <gtest/gtest.h>
#include "termcore/shell_integration.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

using namespace termcore;

// 1. detectShell "/bin/bash" -> Bash
TEST(ShellIntegration, DetectBash) {
    EXPECT_EQ(detectShell("/bin/bash"), ShellType::Bash);
}

// 2. detectShell "/bin/zsh" -> Zsh
TEST(ShellIntegration, DetectZsh) {
    EXPECT_EQ(detectShell("/bin/zsh"), ShellType::Zsh);
}

// 3. detectShell "/usr/bin/fish" -> Fish
TEST(ShellIntegration, DetectFish) {
    EXPECT_EQ(detectShell("/usr/bin/fish"), ShellType::Fish);
}

// 4. detectShell "/bin/sh" -> Unknown
TEST(ShellIntegration, DetectUnknown) {
    EXPECT_EQ(detectShell("/bin/sh"), ShellType::Unknown);
}

// 5. getIntegrationScript Bash contains "133;A"
TEST(ShellIntegration, BashScriptContainsOSC133A) {
    auto script = getIntegrationScript(ShellType::Bash);
    EXPECT_NE(script.find("133;A"), std::string::npos);
}

// 6. getIntegrationScript Zsh contains "precmd"
TEST(ShellIntegration, ZshScriptContainsPrecmd) {
    auto script = getIntegrationScript(ShellType::Zsh);
    EXPECT_NE(script.find("precmd"), std::string::npos);
}

// 7. getIntegrationScript Fish contains "fish_preexec"
TEST(ShellIntegration, FishScriptContainsPreexec) {
    auto script = getIntegrationScript(ShellType::Fish);
    EXPECT_NE(script.find("fish_preexec"), std::string::npos);
}

// 8. getShellEnvVars contains TERM
TEST(ShellIntegration, EnvVarsContainTerm) {
    auto vars = getShellEnvVars();
    bool found = std::any_of(vars.begin(), vars.end(),
        [](const auto& p) { return p.first == "TERM"; });
    EXPECT_TRUE(found);
}

// 9. getSourceCommand generates correct path
TEST(ShellIntegration, SourceCommandPath) {
    auto cmd = getSourceCommand(ShellType::Bash, "/usr/local/share/breadterminal/shell");
    EXPECT_NE(cmd.find("/usr/local/share/breadterminal/shell/bash-integration.sh"),
              std::string::npos);
    EXPECT_NE(cmd.find("source"), std::string::npos);
}

// 10. installScripts creates files in /tmp test dir
TEST(ShellIntegration, InstallScriptsCreatesFiles) {
    std::string testDir = "/tmp/breadterm_test_shell_integration";
    // Clean up first
    std::remove((testDir + "/bash-integration.sh").c_str());
    std::remove((testDir + "/zsh-integration.zsh").c_str());
    std::remove((testDir + "/fish-integration.fish").c_str());
    rmdir(testDir.c_str());

    ASSERT_TRUE(installScripts(testDir));

    struct stat st{};
    EXPECT_EQ(stat((testDir + "/bash-integration.sh").c_str(), &st), 0);
    EXPECT_EQ(stat((testDir + "/zsh-integration.zsh").c_str(), &st), 0);
    EXPECT_EQ(stat((testDir + "/fish-integration.fish").c_str(), &st), 0);

    // Verify content is non-empty
    std::ifstream ifs(testDir + "/bash-integration.sh");
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    EXPECT_FALSE(content.empty());

    // Clean up
    std::remove((testDir + "/bash-integration.sh").c_str());
    std::remove((testDir + "/zsh-integration.zsh").c_str());
    std::remove((testDir + "/fish-integration.fish").c_str());
    rmdir(testDir.c_str());
}

// 11. defaultScriptsDir is non-empty
TEST(ShellIntegration, DefaultScriptsDirNonEmpty) {
    auto dir = defaultScriptsDir();
#if defined(_WIN32)
    if (dir.empty()) GTEST_SKIP() << "HOME not set on Windows";
#endif
    EXPECT_FALSE(dir.empty());
}
