#include <gtest/gtest.h>
#include "termcore/ssh_integration_deploy.h"
#include "termcore/ssh_session.h"

#include <string>

using namespace termcore;

// ---------------------------------------------------------------------------
// SshIntegrationDeploy tests
// ---------------------------------------------------------------------------

TEST(SshIntegrationDeploy, DeployScriptIsNonEmpty) {
    auto script = SshIntegrationDeploy::deployScript();
    EXPECT_FALSE(script.empty());
}

TEST(SshIntegrationDeploy, DeployScriptContainsMkdir) {
    auto script = SshIntegrationDeploy::deployScript();
    EXPECT_NE(script.find("mkdir -p"), std::string::npos);
}

TEST(SshIntegrationDeploy, DeployScriptContainsTic) {
    auto script = SshIntegrationDeploy::deployScript();
    EXPECT_NE(script.find("tic"), std::string::npos);
}

TEST(SshIntegrationDeploy, DeployScriptContainsMarkerFile) {
    auto script = SshIntegrationDeploy::deployScript();
    EXPECT_NE(script.find(".deployed-v1"), std::string::npos);
}

TEST(SshIntegrationDeploy, DeployScriptContainsShellCaseBlock) {
    auto script = SshIntegrationDeploy::deployScript();
    EXPECT_NE(script.find("case"), std::string::npos);
    EXPECT_NE(script.find("bash)"), std::string::npos);
    EXPECT_NE(script.find("zsh)"), std::string::npos);
    EXPECT_NE(script.find("fish)"), std::string::npos);
}

TEST(SshIntegrationDeploy, DeployScriptRespectsRemoteShell) {
    auto script = SshIntegrationDeploy::deployScript("zsh");
    EXPECT_NE(script.find("_BT_SHELL=\"zsh\""), std::string::npos);
}

TEST(SshIntegrationDeploy, DeployScriptUsesDetectionWhenNoShellSpecified) {
    auto script = SshIntegrationDeploy::deployScript();
    EXPECT_NE(script.find("basename"), std::string::npos);
}

TEST(SshIntegrationDeploy, CheckDeployedCommandIsCorrect) {
    auto cmd = SshIntegrationDeploy::checkDeployedCommand();
    EXPECT_NE(cmd.find(".deployed-v1"), std::string::npos);
    EXPECT_NE(cmd.find("DEPLOYED"), std::string::npos);
    EXPECT_NE(cmd.find("NOT_DEPLOYED"), std::string::npos);
}

TEST(SshIntegrationDeploy, DetectShellCommandUsesBasename) {
    auto cmd = SshIntegrationDeploy::detectShellCommand();
    EXPECT_NE(cmd.find("basename"), std::string::npos);
    EXPECT_NE(cmd.find("$SHELL"), std::string::npos);
}

TEST(SshIntegrationDeploy, BashIntegrationContainsOSC133) {
    auto script = SshIntegrationDeploy::bashIntegration();
    EXPECT_FALSE(script.empty());
    EXPECT_NE(script.find("133;A"), std::string::npos);
    EXPECT_NE(script.find("133;C"), std::string::npos);
    EXPECT_NE(script.find("133;D"), std::string::npos);
}

TEST(SshIntegrationDeploy, ZshIntegrationContainsPrecmd) {
    auto script = SshIntegrationDeploy::zshIntegration();
    EXPECT_FALSE(script.empty());
    EXPECT_NE(script.find("precmd"), std::string::npos);
}

TEST(SshIntegrationDeploy, FishIntegrationContainsFishPrompt) {
    auto script = SshIntegrationDeploy::fishIntegration();
    EXPECT_FALSE(script.empty());
    EXPECT_NE(script.find("fish_prompt"), std::string::npos);
}

TEST(SshIntegrationDeploy, TerminfoSourceIsNonEmptyAndContainsBreadterminal) {
    auto source = SshIntegrationDeploy::terminfoSource();
    EXPECT_FALSE(source.empty());
    EXPECT_NE(source.find("breadterminal"), std::string::npos);
}

// ---------------------------------------------------------------------------
// SshSessionConfig tests
// ---------------------------------------------------------------------------

TEST(SshSessionConfig, DefaultAutoDeployIsTrue) {
    SshSessionConfig config;
    EXPECT_TRUE(config.auto_deploy_integration);
}

TEST(SshSessionConfig, DefaultFallbackTerm) {
    SshSessionConfig config;
    EXPECT_EQ(config.fallback_term, "xterm-256color");
}
