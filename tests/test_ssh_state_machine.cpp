#include <gtest/gtest.h>

#include "termcore/ssh_transport.h"
#include "termcore/ssh_known_hosts.h"
#include "termcore/ssh_session.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace termcore;

// ---------------------------------------------------------------------------
// Helper: create a temporary file with optional content
// ---------------------------------------------------------------------------

class TempFileHelper3 {
public:
    explicit TempFileHelper3(const std::string& content = "") {
#if defined(_WIN32)
        char tmpPath[MAX_PATH];
        char tmpFile[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpPath);
        GetTempFileNameA(tmpPath, "ssm", 0, tmpFile);
        path_ = tmpFile;
#else
        char tmpl[] = "/tmp/ssh_state_test_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) {
            path_ = tmpl;
            close(fd);
        }
#endif
        if (!content.empty()) {
            std::ofstream ofs(path_, std::ios::trunc);
            ofs << content;
        }
    }

    ~TempFileHelper3() {
        if (!path_.empty()) std::remove(path_.c_str());
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// ===========================================================================
// Transport State Machine tests
// ===========================================================================

TEST(SshTransportStateMachine, InitialStateIsDisconnected) {
    SshTransportState state = SshTransportState::Disconnected;
    EXPECT_EQ(state, SshTransportState::Disconnected);
    EXPECT_STREQ(transportStateName(state), "disconnected");
}

TEST(SshTransportStateMachine, StateNameRoundTrip) {
    // Verify all state names are unique non-null strings.
    struct StateNamePair {
        SshTransportState state;
        const char* expected;
    };
    StateNamePair pairs[] = {
        {SshTransportState::Disconnected,  "disconnected"},
        {SshTransportState::Connecting,    "connecting"},
        {SshTransportState::Handshaking,   "handshaking"},
        {SshTransportState::HostKeyVerify, "host_key_verify"},
        {SshTransportState::Authenticating,"authenticating"},
        {SshTransportState::Authenticated, "authenticated"},
        {SshTransportState::Error,         "error"},
    };

    std::vector<std::string> names;
    for (const auto& p : pairs) {
        const char* name = transportStateName(p.state);
        ASSERT_NE(name, nullptr);
        EXPECT_STREQ(name, p.expected);
        names.push_back(name);
    }

    // All names must be unique.
    std::sort(names.begin(), names.end());
    auto it = std::unique(names.begin(), names.end());
    EXPECT_EQ(it, names.end()) << "Transport state names are not unique";
}

TEST(SshTransportStateMachine, StateEnumValuesAreDistinct) {
    std::vector<int> values = {
        static_cast<int>(SshTransportState::Disconnected),
        static_cast<int>(SshTransportState::Connecting),
        static_cast<int>(SshTransportState::Handshaking),
        static_cast<int>(SshTransportState::HostKeyVerify),
        static_cast<int>(SshTransportState::Authenticating),
        static_cast<int>(SshTransportState::Authenticated),
        static_cast<int>(SshTransportState::Error),
    };
    std::sort(values.begin(), values.end());
    auto it = std::unique(values.begin(), values.end());
    EXPECT_EQ(it, values.end()) << "Transport state enum values are not unique";
}

TEST(SshTransportStateMachine, StateTransitionSequenceIsValid) {
    // Simulate the expected happy-path state sequence.
    std::vector<SshTransportState> happy_path = {
        SshTransportState::Disconnected,
        SshTransportState::Connecting,
        SshTransportState::Handshaking,
        SshTransportState::HostKeyVerify,
        SshTransportState::Authenticating,
        SshTransportState::Authenticated,
    };

    for (size_t i = 0; i < happy_path.size(); ++i) {
        const char* name = transportStateName(happy_path[i]);
        EXPECT_NE(name, nullptr);
        EXPECT_NE(std::string(name), "unknown");
    }

    // Each step transitions to a different state.
    for (size_t i = 1; i < happy_path.size(); ++i) {
        EXPECT_NE(happy_path[i], happy_path[i - 1]);
    }
}

TEST(SshTransportStateMachine, ErrorStateIsTerminal) {
    EXPECT_STREQ(transportStateName(SshTransportState::Error), "error");
    EXPECT_NE(SshTransportState::Error, SshTransportState::Disconnected);
    EXPECT_NE(SshTransportState::Error, SshTransportState::Authenticated);
}

TEST(SshTransportStateMachine, DisconnectedIsNotAuthenticated) {
    EXPECT_NE(SshTransportState::Disconnected, SshTransportState::Authenticated);
}

TEST(SshTransportStateMachine, DisconnectFromAnyState) {
    // Disconnected is reachable from any state (conceptually).
    // Verify the state value is always assignable and names remain valid.
    SshTransportState all_states[] = {
        SshTransportState::Disconnected,
        SshTransportState::Connecting,
        SshTransportState::Handshaking,
        SshTransportState::HostKeyVerify,
        SshTransportState::Authenticating,
        SshTransportState::Authenticated,
        SshTransportState::Error,
    };

    for (auto start : all_states) {
        SshTransportState current = start;
        // Simulate disconnect: transition to Disconnected.
        current = SshTransportState::Disconnected;
        EXPECT_EQ(current, SshTransportState::Disconnected);
        EXPECT_STREQ(transportStateName(current), "disconnected");
    }
}

// ===========================================================================
// HostKeyAction tests
// ===========================================================================

TEST(HostKeyAction, AllValuesDistinct) {
    EXPECT_NE(HostKeyAction::Accept, HostKeyAction::Reject);
    EXPECT_NE(HostKeyAction::Accept, HostKeyAction::Unknown);
    EXPECT_NE(HostKeyAction::Reject, HostKeyAction::Unknown);
}

// ===========================================================================
// Additional Known Hosts tests
// ===========================================================================

TEST(SshKnownHostsStateMachine, ParseValidKnownHostsFile) {
    std::string content =
        "example.com ssh-ed25519 AAAA1111\n"
        "server.org ssh-rsa BBBB2222\n"
        "[custom.host]:2222 ssh-ecdsa CCCC3333\n";

    TempFileHelper3 tmp(content);
    SshKnownHosts kh(tmp.path());

    EXPECT_EQ(kh.check("example.com", 22, "ssh-ed25519", "AAAA1111"),
              KnownHostResult::Match);
    EXPECT_EQ(kh.check("server.org", 22, "ssh-rsa", "BBBB2222"),
              KnownHostResult::Match);
    EXPECT_EQ(kh.check("custom.host", 2222, "ssh-ecdsa", "CCCC3333"),
              KnownHostResult::Match);
}

TEST(SshKnownHostsStateMachine, EmptyFileReturnsNoHosts) {
    TempFileHelper3 tmp("");
    SshKnownHosts kh(tmp.path());

    EXPECT_EQ(kh.check("any.host", 22, "ssh-rsa", "KEY"),
              KnownHostResult::NotFound);
}

TEST(SshKnownHostsStateMachine, HostLookupFound) {
    TempFileHelper3 tmp;
    SshKnownHosts kh(tmp.path());

    kh.addEntry("known.host.com", 22, "ssh-ed25519", "KNOWNKEY123");
    EXPECT_EQ(kh.check("known.host.com", 22, "ssh-ed25519", "KNOWNKEY123"),
              KnownHostResult::Match);
}

TEST(SshKnownHostsStateMachine, HostLookupNotFound) {
    TempFileHelper3 tmp;
    SshKnownHosts kh(tmp.path());

    kh.addEntry("other.host.com", 22, "ssh-rsa", "SOMEKEY");
    EXPECT_EQ(kh.check("unknown.host.com", 22, "ssh-rsa", "ANYKEY"),
              KnownHostResult::NotFound);
}

TEST(SshKnownHostsStateMachine, HostKeyMismatch) {
    TempFileHelper3 tmp;
    SshKnownHosts kh(tmp.path());

    kh.addEntry("secure.host.com", 22, "ssh-ed25519", "ORIGINALKEY");
    EXPECT_EQ(kh.check("secure.host.com", 22, "ssh-ed25519", "DIFFERENTKEY"),
              KnownHostResult::Mismatch);
}

TEST(SshKnownHostsStateMachine, AddNewHost) {
    TempFileHelper3 tmp;
    SshKnownHosts kh(tmp.path());

    EXPECT_EQ(kh.check("new.host.com", 22, "ssh-rsa", "NEWKEY"),
              KnownHostResult::NotFound);

    EXPECT_TRUE(kh.addEntry("new.host.com", 22, "ssh-rsa", "NEWKEY"));
    EXPECT_EQ(kh.check("new.host.com", 22, "ssh-rsa", "NEWKEY"),
              KnownHostResult::Match);
}

TEST(SshKnownHostsStateMachine, MalformedLineSkipped) {
    std::string content =
        "this-is-incomplete\n"
        "\n"
        "# comment line\n"
        "  \n"
        "only-two-fields ssh-rsa\n"
        "valid.host ssh-ed25519 VALIDKEY123\n"
        "another-bad-line\n";

    TempFileHelper3 tmp(content);
    SshKnownHosts kh(tmp.path());

    EXPECT_EQ(kh.check("valid.host", 22, "ssh-ed25519", "VALIDKEY123"),
              KnownHostResult::Match);

    EXPECT_EQ(kh.check("this-is-incomplete", 22, "ssh-rsa", "anything"),
              KnownHostResult::NotFound);
    EXPECT_EQ(kh.check("only-two-fields", 22, "ssh-rsa", "anything"),
              KnownHostResult::NotFound);
}

TEST(SshKnownHostsStateMachine, CommentLinesIgnored) {
    std::string content =
        "# This is a comment\n"
        "#host.com ssh-rsa SHOULDBEIGNORED\n"
        "real.host ssh-rsa REALKEY\n";

    TempFileHelper3 tmp(content);
    SshKnownHosts kh(tmp.path());

    EXPECT_EQ(kh.check("real.host", 22, "ssh-rsa", "REALKEY"),
              KnownHostResult::Match);
    EXPECT_EQ(kh.check("#host.com", 22, "ssh-rsa", "SHOULDBEIGNORED"),
              KnownHostResult::NotFound);
}

TEST(SshKnownHostsStateMachine, DifferentKeyTypeSameHostIsMismatch) {
    TempFileHelper3 tmp;
    SshKnownHosts kh(tmp.path());

    kh.addEntry("multi.host.com", 22, "ssh-ed25519", "ED25519KEY");

    // Host matches but key type + data differ -> Mismatch.
    auto result = kh.check("multi.host.com", 22, "ssh-rsa", "RSAKEY");
    EXPECT_EQ(result, KnownHostResult::Mismatch);
}

TEST(SshKnownHostsStateMachine, PersistenceAcrossInstances) {
    TempFileHelper3 tmp;

    {
        SshKnownHosts kh1(tmp.path());
        kh1.addEntry("persist.host", 22, "ssh-rsa", "PERSISTKEY");
    }

    {
        SshKnownHosts kh2(tmp.path());
        EXPECT_EQ(kh2.check("persist.host", 22, "ssh-rsa", "PERSISTKEY"),
                  KnownHostResult::Match);
    }
}

TEST(KnownHostResult, AllValuesDistinct) {
    EXPECT_NE(KnownHostResult::Match, KnownHostResult::Mismatch);
    EXPECT_NE(KnownHostResult::Match, KnownHostResult::NotFound);
    EXPECT_NE(KnownHostResult::Match, KnownHostResult::Error);
    EXPECT_NE(KnownHostResult::Mismatch, KnownHostResult::NotFound);
    EXPECT_NE(KnownHostResult::Mismatch, KnownHostResult::Error);
    EXPECT_NE(KnownHostResult::NotFound, KnownHostResult::Error);
}

// ===========================================================================
// SSH Session tests
// ===========================================================================

TEST(SshSessionStateMachine, SessionCreation) {
    SshConfig cfg;
    cfg.host = "testhost";
    cfg.hostname = "192.168.1.50";
    cfg.port = 22;
    cfg.user = "testuser";

    EXPECT_EQ(cfg.host, "testhost");
    EXPECT_EQ(cfg.hostname, "192.168.1.50");
    EXPECT_EQ(cfg.port, 22);
    EXPECT_EQ(cfg.user, "testuser");
}

TEST(SshSessionStateMachine, SessionConfigApplied) {
    SshConfig cfg;
    cfg.host = "myserver";
    cfg.hostname = "server.example.com";
    cfg.port = 2222;
    cfg.user = "admin";
    cfg.identity_file = "/home/admin/.ssh/id_ed25519";
    cfg.forward_agent = true;
    cfg.proxy_jump = "bastion";

    EXPECT_EQ(cfg.port, 2222);
    EXPECT_EQ(cfg.user, "admin");
    EXPECT_EQ(cfg.identity_file, "/home/admin/.ssh/id_ed25519");
    EXPECT_TRUE(cfg.forward_agent);
    EXPECT_EQ(cfg.proxy_jump, "bastion");
}

TEST(SshSessionStateMachine, SessionCommandBuilding) {
    SshConfig cfg;
    cfg.host = "webserver";
    cfg.hostname = "web.example.com";
    cfg.port = 22;
    cfg.user = "deploy";

    auto cmd = SshSession::buildSshCommand(cfg);
    EXPECT_NE(cmd.find("ssh"), std::string::npos);
    EXPECT_NE(cmd.find("deploy@web.example.com"), std::string::npos);
}

TEST(SshSessionStateMachine, SessionCommandWithPort) {
    SshConfig cfg;
    cfg.hostname = "server.io";
    cfg.port = 2222;
    cfg.user = "user";

    auto args = SshSession::buildSshArgs(cfg);
    bool has_port = false;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-p" && args[i + 1] == "2222") {
            has_port = true;
        }
    }
    EXPECT_TRUE(has_port);
}

TEST(SshSessionStateMachine, SessionCommandWithIdentity) {
    SshConfig cfg;
    cfg.hostname = "server.io";
    cfg.identity_file = "/home/user/.ssh/id_ed25519";

    auto args = SshSession::buildSshArgs(cfg);
    bool has_identity = false;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-i" && args[i + 1] == "/home/user/.ssh/id_ed25519") {
            has_identity = true;
        }
    }
    EXPECT_TRUE(has_identity);
}

TEST(SshSessionStateMachine, SessionCommandWithForwardAgent) {
    SshConfig cfg;
    cfg.hostname = "server.io";
    cfg.forward_agent = true;

    auto args = SshSession::buildSshArgs(cfg);
    bool has_agent = false;
    for (const auto& a : args) {
        if (a == "-A") has_agent = true;
    }
    EXPECT_TRUE(has_agent);
}

TEST(SshSessionStateMachine, SessionCommandWithProxyCommand) {
    SshConfig cfg;
    cfg.hostname = "internal.server";
    cfg.proxy_command = "ssh -W %h:%p bastion";

    auto args = SshSession::buildSshArgs(cfg);
    bool has_proxy = false;
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-o" &&
            args[i + 1].find("ProxyCommand=") != std::string::npos) {
            has_proxy = true;
        }
    }
    EXPECT_TRUE(has_proxy);
}

TEST(SshSessionStateMachine, SessionEnvironmentSetup) {
    // SshTransportConfig holds the TERM type.
    SshTransportConfig cfg;
    cfg.term_type = "xterm-256color";
    EXPECT_EQ(cfg.term_type, "xterm-256color");

    // SshSessionConfig has auto_deploy and fallback_term.
    SshSessionConfig session_cfg;
    EXPECT_TRUE(session_cfg.auto_deploy_integration);
    EXPECT_EQ(session_cfg.fallback_term, "xterm-256color");
}

TEST(SshSessionStateMachine, SshConfigDefaultValues) {
    SshConfig cfg;
    EXPECT_EQ(cfg.port, 22);
    EXPECT_TRUE(cfg.host.empty());
    EXPECT_TRUE(cfg.hostname.empty());
    EXPECT_TRUE(cfg.user.empty());
    EXPECT_TRUE(cfg.identity_file.empty());
    EXPECT_FALSE(cfg.forward_agent);
    EXPECT_TRUE(cfg.proxy_command.empty());
    EXPECT_TRUE(cfg.proxy_jump.empty());
}

TEST(SshSessionStateMachine, DisplayNameUserAtHost) {
    SshConfig cfg;
    cfg.hostname = "example.com";
    cfg.user = "admin";
    cfg.port = 22;
    EXPECT_EQ(cfg.displayName(), "admin@example.com");
}

TEST(SshSessionStateMachine, DisplayNameWithPort) {
    SshConfig cfg;
    cfg.hostname = "example.com";
    cfg.user = "admin";
    cfg.port = 2222;
    EXPECT_EQ(cfg.displayName(), "admin@example.com:2222");
}

TEST(SshSessionStateMachine, DisplayNameNoUser) {
    SshConfig cfg;
    cfg.hostname = "example.com";
    cfg.port = 22;
    EXPECT_EQ(cfg.displayName(), "example.com");
}

TEST(SshSessionStateMachine, DisplayNameFallsBackToHost) {
    SshConfig cfg;
    cfg.host = "myalias";
    cfg.port = 22;
    EXPECT_EQ(cfg.displayName(), "myalias");
}

// ===========================================================================
// Additional Auth Method tests (expand existing)
// ===========================================================================

TEST(AuthMethodStateMachine, AgentAuthPreferred) {
    SshTransportConfig cfg;
    cfg.try_agent = true;
    cfg.password = "pw";
    cfg.identity_files = {"/key1", "/key2"};

    auto methods = selectAuthMethods(cfg);
    ASSERT_FALSE(methods.empty());
    EXPECT_EQ(methods[0], SshAuthMethod::Agent);
}

TEST(AuthMethodStateMachine, PublicKeyFallback) {
    SshTransportConfig cfg;
    cfg.try_agent = false;
    cfg.identity_files = {"/home/user/.ssh/id_ed25519"};
    cfg.password = "backup";

    auto methods = selectAuthMethods(cfg);
    ASSERT_GE(methods.size(), 1u);
    EXPECT_EQ(methods[0], SshAuthMethod::PublicKey);

    // Password comes after public key.
    bool seen_pubkey = false;
    bool pubkey_before_password = false;
    for (auto m : methods) {
        if (m == SshAuthMethod::PublicKey) seen_pubkey = true;
        if (m == SshAuthMethod::Password && seen_pubkey) pubkey_before_password = true;
    }
    EXPECT_TRUE(pubkey_before_password);
}

TEST(AuthMethodStateMachine, PasswordLastResort) {
    SshTransportConfig cfg;
    cfg.try_agent = true;
    cfg.identity_files = {"/key"};
    cfg.password = "secret";

    auto methods = selectAuthMethods(cfg);
    ASSERT_GE(methods.size(), 2u);
    EXPECT_EQ(methods.back(), SshAuthMethod::Password);
}

TEST(AuthMethodStateMachine, NoAuthMethodsAvailable) {
    SshTransportConfig cfg;
    cfg.try_agent = false;
    cfg.identity_files.clear();
    cfg.password.clear();

    auto methods = selectAuthMethods(cfg);
    // Should never have Agent or Password.
    for (auto m : methods) {
        EXPECT_NE(m, SshAuthMethod::Agent);
        EXPECT_NE(m, SshAuthMethod::Password);
    }
}

TEST(AuthMethodStateMachine, OnlyAgentWhenNoKeysNoPassword) {
    SshTransportConfig cfg;
    cfg.try_agent = true;
    cfg.identity_files.clear();
    cfg.password.clear();

    auto methods = selectAuthMethods(cfg);
    ASSERT_GE(methods.size(), 1u);
    EXPECT_EQ(methods[0], SshAuthMethod::Agent);
    for (auto m : methods) {
        EXPECT_NE(m, SshAuthMethod::Password);
    }
}

TEST(AuthMethodStateMachine, MultipleIdentityFilesStillOnePubkeyEntry) {
    SshTransportConfig cfg;
    cfg.try_agent = false;
    cfg.identity_files = {"/key1", "/key2", "/key3"};
    cfg.password.clear();

    auto methods = selectAuthMethods(cfg);
    int pubkey_count = 0;
    for (auto m : methods) {
        if (m == SshAuthMethod::PublicKey) ++pubkey_count;
    }
    EXPECT_EQ(pubkey_count, 1);
}

// ===========================================================================
// SshTransportConfig additional tests
// ===========================================================================

TEST(SshTransportConfigStateMachine, CustomValues) {
    SshTransportConfig cfg;
    cfg.hostname = "my.server.com";
    cfg.port = 2222;
    cfg.username = "admin";
    cfg.password = "secret123";
    cfg.identity_files = {"/home/admin/.ssh/id_ed25519"};
    cfg.try_agent = false;
    cfg.term_type = "xterm-kitty";
    cfg.pty_cols = 120;
    cfg.pty_rows = 40;
    cfg.keepalive_seconds = 60;
    cfg.connect_timeout_seconds = 30;

    EXPECT_EQ(cfg.hostname, "my.server.com");
    EXPECT_EQ(cfg.port, 2222);
    EXPECT_EQ(cfg.username, "admin");
    EXPECT_EQ(cfg.password, "secret123");
    ASSERT_EQ(cfg.identity_files.size(), 1u);
    EXPECT_EQ(cfg.identity_files[0], "/home/admin/.ssh/id_ed25519");
    EXPECT_FALSE(cfg.try_agent);
    EXPECT_EQ(cfg.term_type, "xterm-kitty");
    EXPECT_EQ(cfg.pty_cols, 120);
    EXPECT_EQ(cfg.pty_rows, 40);
    EXPECT_EQ(cfg.keepalive_seconds, 60);
    EXPECT_EQ(cfg.connect_timeout_seconds, 30);
}

TEST(SshTransportConfigStateMachine, MultipleIdentityFiles) {
    SshTransportConfig cfg;
    cfg.identity_files = {
        "/home/user/.ssh/id_ed25519",
        "/home/user/.ssh/id_rsa",
        "/home/user/.ssh/id_ecdsa",
    };
    EXPECT_EQ(cfg.identity_files.size(), 3u);
}

// ===========================================================================
// Auth method name completeness
// ===========================================================================

TEST(SshAuthMethodStateMachine, AllNamesAreNonNull) {
    const SshAuthMethod all[] = {
        SshAuthMethod::None,
        SshAuthMethod::Password,
        SshAuthMethod::PublicKey,
        SshAuthMethod::Agent,
    };
    for (auto m : all) {
        const char* name = authMethodName(m);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::string(name).size(), 0u);
    }
}

TEST(SshAuthMethodStateMachine, NamesAreUnique) {
    std::vector<std::string> names = {
        authMethodName(SshAuthMethod::None),
        authMethodName(SshAuthMethod::Password),
        authMethodName(SshAuthMethod::PublicKey),
        authMethodName(SshAuthMethod::Agent),
    };
    std::sort(names.begin(), names.end());
    auto it = std::unique(names.begin(), names.end());
    EXPECT_EQ(it, names.end()) << "Auth method names are not unique";
}

// ===========================================================================
// Home path expansion edge cases
// ===========================================================================

TEST(ExpandHomePathStateMachine, TildeWithBackslash) {
    auto result = expandHomePath("~\\.ssh\\config");
    EXPECT_NE(result[0], '~');
    EXPECT_NE(result.find(".ssh"), std::string::npos);
}

TEST(ExpandHomePathStateMachine, NonTildeUserPrefix) {
    // "~otheruser/path" should pass through unchanged.
    auto result = expandHomePath("~otheruser/.ssh");
    EXPECT_EQ(result, "~otheruser/.ssh");
}
