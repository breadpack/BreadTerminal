#include "termcore/ssh_session.h"
#include "termcore/ssh_terminfo.h"
#include "termcore/ssh_profile.h"
#include "termcore/ssh_integration_deploy.h"
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace termcore;

// ---------------------------------------------------------------------------
// Helper: create a temporary file with given content
// ---------------------------------------------------------------------------

class TempFile {
public:
    explicit TempFile(const std::string& content) {
#if defined(_WIN32)
        char tmpPath[MAX_PATH];
        char tmpFile[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpPath);
        GetTempFileNameA(tmpPath, "ssh", 0, tmpFile);
        path_ = tmpFile;
#else
        char tmpl[] = "/tmp/ssh_test_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) {
            path_ = tmpl;
            close(fd);
        }
#endif
        std::ofstream ofs(path_, std::ios::trunc);
        ofs << content;
    }

    ~TempFile() {
        if (!path_.empty()) std::remove(path_.c_str());
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// ===========================================================================
// SshSession tests
// ===========================================================================

TEST(SshSession, ParseBasicConfig) {
    TempFile cfg(
        "Host myserver\n"
        "    HostName 192.168.1.100\n"
        "    Port 2222\n"
        "    User admin\n"
        "    IdentityFile ~/.ssh/id_myserver\n"
        "\n"
        "Host devbox\n"
        "    HostName dev.example.com\n"
        "    User developer\n"
        "    ForwardAgent yes\n"
    );

    auto configs = SshSession::parseSSHConfig(cfg.path());
    ASSERT_EQ(configs.size(), 2u);

    EXPECT_EQ(configs[0].host, "myserver");
    EXPECT_EQ(configs[0].hostname, "192.168.1.100");
    EXPECT_EQ(configs[0].port, 2222);
    EXPECT_EQ(configs[0].user, "admin");
    EXPECT_FALSE(configs[0].identity_file.empty());
    EXPECT_FALSE(configs[0].forward_agent);

    EXPECT_EQ(configs[1].host, "devbox");
    EXPECT_EQ(configs[1].hostname, "dev.example.com");
    EXPECT_EQ(configs[1].port, 22);
    EXPECT_EQ(configs[1].user, "developer");
    EXPECT_TRUE(configs[1].forward_agent);
}

TEST(SshSession, WildcardHostSkipped) {
    TempFile cfg(
        "Host *\n"
        "    ServerAliveInterval 60\n"
        "\n"
        "Host realhost\n"
        "    HostName example.com\n"
    );

    auto configs = SshSession::parseSSHConfig(cfg.path());
    ASSERT_EQ(configs.size(), 1u);
    EXPECT_EQ(configs[0].host, "realhost");
}

TEST(SshSession, ProxyCommandAndJump) {
    TempFile cfg(
        "Host jumpbox\n"
        "    HostName jump.example.com\n"
        "    User jumpuser\n"
        "\n"
        "Host internal\n"
        "    HostName 10.0.0.5\n"
        "    ProxyJump jumpbox\n"
        "\n"
        "Host legacy\n"
        "    HostName old.example.com\n"
        "    ProxyCommand ssh -W %h:%p jumpbox\n"
    );

    auto configs = SshSession::parseSSHConfig(cfg.path());
    ASSERT_EQ(configs.size(), 3u);

    EXPECT_EQ(configs[1].host, "internal");
    EXPECT_EQ(configs[1].proxy_jump, "jumpbox");

    EXPECT_EQ(configs[2].host, "legacy");
    EXPECT_EQ(configs[2].proxy_command, "ssh -W %h:%p jumpbox");
}

TEST(SshSession, CommentsAndEmptyLines) {
    TempFile cfg(
        "# This is a comment\n"
        "\n"
        "Host commented\n"
        "    # HostName should.be.ignored\n"
        "    HostName actual.host.com\n"
        "    # Port 9999\n"
        "    Port 8080\n"
    );

    auto configs = SshSession::parseSSHConfig(cfg.path());
    ASSERT_EQ(configs.size(), 1u);
    EXPECT_EQ(configs[0].hostname, "actual.host.com");
    EXPECT_EQ(configs[0].port, 8080);
}

TEST(SshSession, EqualsSignSeparator) {
    TempFile cfg(
        "Host equalshost\n"
        "    HostName=eq.example.com\n"
        "    Port=3322\n"
        "    User=equser\n"
    );

    auto configs = SshSession::parseSSHConfig(cfg.path());
    ASSERT_EQ(configs.size(), 1u);
    EXPECT_EQ(configs[0].hostname, "eq.example.com");
    EXPECT_EQ(configs[0].port, 3322);
    EXPECT_EQ(configs[0].user, "equser");
}

TEST(SshSession, NonexistentFile) {
    auto configs = SshSession::parseSSHConfig("/nonexistent/path/config");
    EXPECT_TRUE(configs.empty());
}

// ---------------------------------------------------------------------------
// Pattern matching tests
// ---------------------------------------------------------------------------

TEST(SshSession, MatchHostPatternExact) {
    EXPECT_TRUE(SshSession::matchHostPattern("myhost", "myhost"));
    EXPECT_FALSE(SshSession::matchHostPattern("myhost", "otherhost"));
}

TEST(SshSession, MatchHostPatternWildcard) {
    EXPECT_TRUE(SshSession::matchHostPattern("*.example.com", "server.example.com"));
    EXPECT_TRUE(SshSession::matchHostPattern("*.example.com", "a.b.example.com"));
    EXPECT_FALSE(SshSession::matchHostPattern("*.example.com", "example.com"));
    EXPECT_FALSE(SshSession::matchHostPattern("*.example.com", "other.com"));
}

TEST(SshSession, MatchHostPatternQuestion) {
    EXPECT_TRUE(SshSession::matchHostPattern("host?", "host1"));
    EXPECT_TRUE(SshSession::matchHostPattern("host?", "hostA"));
    EXPECT_FALSE(SshSession::matchHostPattern("host?", "host"));
    EXPECT_FALSE(SshSession::matchHostPattern("host?", "host12"));
}

TEST(SshSession, MatchHostPatternComplex) {
    EXPECT_TRUE(SshSession::matchHostPattern("prod-*-us", "prod-web-us"));
    EXPECT_TRUE(SshSession::matchHostPattern("prod-*-us", "prod-db-server-us"));
    EXPECT_FALSE(SshSession::matchHostPattern("prod-*-us", "prod-web-eu"));
}

// ---------------------------------------------------------------------------
// Command building tests
// ---------------------------------------------------------------------------

TEST(SshSession, BuildSshCommandBasic) {
    SshConfig cfg;
    cfg.host = "myhost";
    cfg.hostname = "192.168.1.1";

    auto cmd = SshSession::buildSshCommand(cfg);
    EXPECT_EQ(cmd, "ssh 192.168.1.1");
}

TEST(SshSession, BuildSshCommandFull) {
    SshConfig cfg;
    cfg.host = "myserver";
    cfg.hostname = "server.example.com";
    cfg.port = 2222;
    cfg.user = "admin";
    cfg.identity_file = "/home/user/.ssh/id_rsa";
    cfg.forward_agent = true;

    auto args = SshSession::buildSshArgs(cfg);
    ASSERT_GE(args.size(), 6u);
    EXPECT_EQ(args[0], "ssh");

    bool has_port = false, has_identity = false, has_agent = false;
    std::string dest;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-p" && i + 1 < args.size()) {
            EXPECT_EQ(args[i + 1], "2222");
            has_port = true;
        }
        if (args[i] == "-i" && i + 1 < args.size()) {
            EXPECT_EQ(args[i + 1], "/home/user/.ssh/id_rsa");
            has_identity = true;
        }
        if (args[i] == "-A") has_agent = true;
    }
    EXPECT_TRUE(has_port);
    EXPECT_TRUE(has_identity);
    EXPECT_TRUE(has_agent);

    EXPECT_EQ(args.back(), "admin@server.example.com");
}

TEST(SshSession, BuildSshCommandWithProxyJump) {
    SshConfig cfg;
    cfg.host = "internal";
    cfg.hostname = "10.0.0.5";
    cfg.proxy_jump = "jumpbox";

    auto args = SshSession::buildSshArgs(cfg);
    bool has_jump = false;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-J" && i + 1 < args.size()) {
            EXPECT_EQ(args[i + 1], "jumpbox");
            has_jump = true;
        }
    }
    EXPECT_TRUE(has_jump);
}

TEST(SshSession, FindHostExact) {
    std::vector<SshConfig> configs;
    configs.push_back({"server1", "s1.example.com", 22, "user1", "", false, "", ""});
    configs.push_back({"server2", "s2.example.com", 22, "user2", "", false, "", ""});

    auto* found = SshSession::findHost(configs, "server2");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->hostname, "s2.example.com");
}

TEST(SshSession, FindHostPattern) {
    std::vector<SshConfig> configs;
    configs.push_back({"*.prod.com", "prod-generic.com", 22, "produser", "", false, "", ""});

    auto* found = SshSession::findHost(configs, "web.prod.com");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->user, "produser");

    auto* notfound = SshSession::findHost(configs, "staging.dev.com");
    EXPECT_EQ(notfound, nullptr);
}

TEST(SshSession, DisplayName) {
    SshConfig cfg;
    cfg.host = "myhost";
    cfg.hostname = "real.host.com";
    cfg.user = "admin";
    cfg.port = 22;
    EXPECT_EQ(cfg.displayName(), "admin@real.host.com");

    cfg.port = 2222;
    EXPECT_EQ(cfg.displayName(), "admin@real.host.com:2222");

    cfg.user.clear();
    EXPECT_EQ(cfg.displayName(), "real.host.com:2222");

    cfg.hostname.clear();
    cfg.port = 22;
    EXPECT_EQ(cfg.displayName(), "myhost");
}

// ===========================================================================
// SshTerminfoHelper tests
// ===========================================================================

TEST(SshTerminfoHelper, Base64TerminfoNotEmpty) {
    auto b64 = SshTerminfoHelper::getBase64TerminfoSource();
    EXPECT_FALSE(b64.empty());
    for (char c : b64) {
        bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        EXPECT_TRUE(valid) << "Invalid base64 character: " << c;
    }
}

TEST(SshTerminfoHelper, GenerateInstallScript) {
    auto script = SshTerminfoHelper::generateTerminfoInstallScript();
    EXPECT_FALSE(script.empty());
    EXPECT_NE(script.find("xterm-breadterminal"), std::string::npos);
    EXPECT_NE(script.find("base64"), std::string::npos);
    EXPECT_NE(script.find("TERM_PROGRAM=BreadTerminal"), std::string::npos);
    EXPECT_NE(script.find("BREADTERMINAL=1"), std::string::npos);
    EXPECT_NE(script.find("tic"), std::string::npos);
    EXPECT_NE(script.find(".terminfo"), std::string::npos);
    EXPECT_NE(script.find("exec"), std::string::npos);
}

TEST(SshTerminfoHelper, WrapSshCommand) {
    auto wrapped = SshTerminfoHelper::wrapSshCommand("ssh user@host");
    EXPECT_NE(wrapped.find("ssh user@host"), std::string::npos);
    EXPECT_NE(wrapped.find("sh -c"), std::string::npos);
    EXPECT_NE(wrapped.find("xterm-breadterminal"), std::string::npos);
}

TEST(SshTerminfoHelper, WrapSshArgs) {
    std::vector<std::string> base = {"ssh", "-p", "22", "user@host"};
    auto wrapped = SshTerminfoHelper::wrapSshArgs(base);

    ASSERT_GT(wrapped.size(), base.size());
    EXPECT_EQ(wrapped[0], "ssh");
    EXPECT_EQ(wrapped[1], "-p");
    EXPECT_EQ(wrapped[2], "22");
    EXPECT_EQ(wrapped[3], "user@host");
    EXPECT_EQ(wrapped[4], "--");
    EXPECT_EQ(wrapped[5], "sh");
    EXPECT_EQ(wrapped[6], "-c");
    EXPECT_NE(wrapped[7].find("xterm-breadterminal"), std::string::npos);
}

TEST(SshTerminfoHelper, RemoteEnvSetup) {
    auto env = SshTerminfoHelper::generateRemoteEnvSetup();
    EXPECT_NE(env.find("TERM_PROGRAM=BreadTerminal"), std::string::npos);
    EXPECT_NE(env.find("BREADTERMINAL=1"), std::string::npos);
}

// ===========================================================================
// SshProfileManager tests
// ===========================================================================

TEST(SshProfileManager, DiscoverFromConfig) {
    TempFile cfg(
        "Host webserver\n"
        "    HostName web.example.com\n"
        "    User deploy\n"
        "\n"
        "Host dbserver\n"
        "    HostName db.example.com\n"
        "    Port 5432\n"
    );

    SshProfileManager mgr;
    mgr.discoverFromSSHConfig(cfg.path());

    auto& profiles = mgr.profiles();
    ASSERT_EQ(profiles.size(), 2u);
    EXPECT_EQ(profiles[0].name, "webserver");
    EXPECT_EQ(profiles[1].name, "dbserver");
}

TEST(SshProfileManager, AddAndFindProfile) {
    SshProfileManager mgr;

    SshProfile p;
    p.name = "test-server";
    p.config.host = "test-server";
    p.config.hostname = "test.example.com";
    p.config.user = "tester";
    mgr.addProfile(p);

    auto* found = mgr.findProfile("test-server");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->config.hostname, "test.example.com");

    auto* notfound = mgr.findProfile("nonexistent");
    EXPECT_EQ(notfound, nullptr);
}

TEST(SshProfileManager, RemoveProfile) {
    SshProfileManager mgr;

    SshProfile p;
    p.name = "to-remove";
    p.config.host = "to-remove";
    mgr.addProfile(p);

    EXPECT_EQ(mgr.profiles().size(), 1u);
    EXPECT_TRUE(mgr.removeProfile("to-remove"));
    EXPECT_TRUE(mgr.profiles().empty());
    EXPECT_FALSE(mgr.removeProfile("to-remove"));
}

TEST(SshProfileManager, AddDuplicateReplaces) {
    SshProfileManager mgr;

    SshProfile p1;
    p1.name = "server";
    p1.config.host = "server";
    p1.config.hostname = "old.example.com";
    mgr.addProfile(p1);

    SshProfile p2;
    p2.name = "server";
    p2.config.host = "server";
    p2.config.hostname = "new.example.com";
    mgr.addProfile(p2);

    EXPECT_EQ(mgr.profiles().size(), 1u);
    auto* found = mgr.findProfile("server");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->config.hostname, "new.example.com");
}

TEST(SshProfileManager, DiscoverSkipsDuplicates) {
    TempFile cfg(
        "Host server1\n"
        "    HostName s1.example.com\n"
    );

    SshProfileManager mgr;

    SshProfile p;
    p.name = "server1";
    p.config.host = "server1";
    p.config.hostname = "original.example.com";
    mgr.addProfile(p);

    mgr.discoverFromSSHConfig(cfg.path());

    EXPECT_EQ(mgr.profiles().size(), 1u);
    EXPECT_EQ(mgr.findProfile("server1")->config.hostname, "original.example.com");
}

TEST(SshProfileManager, DefaultSSHConfigPath) {
    auto path = SshProfileManager::defaultSSHConfigPath();
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("ssh"), std::string::npos);
    EXPECT_NE(path.find("config"), std::string::npos);
}

// ===========================================================================
// SshIntegrationDeploy tests
// ===========================================================================

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
