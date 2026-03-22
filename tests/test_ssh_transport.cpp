#include <gtest/gtest.h>

#include "termcore/ssh_transport.h"
#include "termcore/ssh_known_hosts.h"

#include <cstdio>
#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace termcore;

// ---------------------------------------------------------------------------
// Helper: create a temporary file
// ---------------------------------------------------------------------------

class TempFileHelper2 {
public:
    explicit TempFileHelper2(const std::string& content = "") {
#if defined(_WIN32)
        char tmpPath[MAX_PATH];
        char tmpFile[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpPath);
        GetTempFileNameA(tmpPath, "ssh", 0, tmpFile);
        path_ = tmpFile;
#else
        char tmpl[] = "/tmp/ssh_transport_test_XXXXXX";
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

    ~TempFileHelper2() {
        if (!path_.empty()) std::remove(path_.c_str());
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// ===========================================================================
// SshTransport unit tests (no real SSH server required)
// ===========================================================================

// ---------------------------------------------------------------------------
// Transport state names
// ---------------------------------------------------------------------------

TEST(SshTransportState, StateNames) {
    EXPECT_STREQ(transportStateName(SshTransportState::Disconnected), "disconnected");
    EXPECT_STREQ(transportStateName(SshTransportState::Connecting), "connecting");
    EXPECT_STREQ(transportStateName(SshTransportState::Handshaking), "handshaking");
    EXPECT_STREQ(transportStateName(SshTransportState::HostKeyVerify), "host_key_verify");
    EXPECT_STREQ(transportStateName(SshTransportState::Authenticating), "authenticating");
    EXPECT_STREQ(transportStateName(SshTransportState::Authenticated), "authenticated");
    EXPECT_STREQ(transportStateName(SshTransportState::Error), "error");
}

// ---------------------------------------------------------------------------
// Auth method names
// ---------------------------------------------------------------------------

TEST(SshAuthMethod, Names) {
    EXPECT_STREQ(authMethodName(SshAuthMethod::None), "none");
    EXPECT_STREQ(authMethodName(SshAuthMethod::Password), "password");
    EXPECT_STREQ(authMethodName(SshAuthMethod::PublicKey), "publickey");
    EXPECT_STREQ(authMethodName(SshAuthMethod::Agent), "agent");
}

// ---------------------------------------------------------------------------
// Auth method selection logic
// ---------------------------------------------------------------------------

TEST(AuthMethodSelection, AgentFirstWhenEnabled) {
    SshTransportConfig cfg;
    cfg.try_agent = true;
    cfg.password = "secret";
    cfg.identity_files = {"/some/key"};

    auto methods = selectAuthMethods(cfg);
    ASSERT_GE(methods.size(), 1u);
    EXPECT_EQ(methods[0], SshAuthMethod::Agent);
}

TEST(AuthMethodSelection, NoAgentWhenDisabled) {
    SshTransportConfig cfg;
    cfg.try_agent = false;
    cfg.password = "secret";

    auto methods = selectAuthMethods(cfg);
    for (auto m : methods) {
        EXPECT_NE(m, SshAuthMethod::Agent);
    }
}

TEST(AuthMethodSelection, PublicKeyWhenIdentityProvided) {
    SshTransportConfig cfg;
    cfg.try_agent = false;
    cfg.identity_files = {"/home/user/.ssh/id_rsa"};

    auto methods = selectAuthMethods(cfg);
    bool has_pk = false;
    for (auto m : methods) {
        if (m == SshAuthMethod::PublicKey) has_pk = true;
    }
    EXPECT_TRUE(has_pk);
}

TEST(AuthMethodSelection, PasswordWhenProvided) {
    SshTransportConfig cfg;
    cfg.try_agent = false;
    cfg.password = "mypass";

    auto methods = selectAuthMethods(cfg);
    bool has_pw = false;
    for (auto m : methods) {
        if (m == SshAuthMethod::Password) has_pw = true;
    }
    EXPECT_TRUE(has_pw);
}

TEST(AuthMethodSelection, NoPasswordWhenEmpty) {
    SshTransportConfig cfg;
    cfg.try_agent = false;
    cfg.password = "";

    auto methods = selectAuthMethods(cfg);
    for (auto m : methods) {
        EXPECT_NE(m, SshAuthMethod::Password);
    }
}

TEST(AuthMethodSelection, OrderIsAgentPubkeyPassword) {
    SshTransportConfig cfg;
    cfg.try_agent = true;
    cfg.identity_files = {"/key"};
    cfg.password = "pw";

    auto methods = selectAuthMethods(cfg);
    ASSERT_EQ(methods.size(), 3u);
    EXPECT_EQ(methods[0], SshAuthMethod::Agent);
    EXPECT_EQ(methods[1], SshAuthMethod::PublicKey);
    EXPECT_EQ(methods[2], SshAuthMethod::Password);
}

// ---------------------------------------------------------------------------
// Home path expansion
// ---------------------------------------------------------------------------

TEST(ExpandHomePath, NonTildePassthrough) {
    EXPECT_EQ(expandHomePath("/absolute/path"), "/absolute/path");
    EXPECT_EQ(expandHomePath("relative/path"), "relative/path");
    EXPECT_EQ(expandHomePath(""), "");
}

TEST(ExpandHomePath, TildeExpands) {
    auto result = expandHomePath("~/.ssh/id_rsa");
    EXPECT_NE(result[0], '~');
    auto pos = result.find(".ssh");
    EXPECT_NE(pos, std::string::npos);
}

TEST(ExpandHomePath, TildeAloneIsHome) {
    auto result = expandHomePath("~");
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result, "~");
}

// ---------------------------------------------------------------------------
// Default identity files
// ---------------------------------------------------------------------------

TEST(DefaultIdentityFiles, ReturnsVector) {
    auto files = defaultIdentityFiles();
    for (const auto& f : files) {
        EXPECT_FALSE(f.empty());
        EXPECT_NE(f[0], '~');
    }
}

// ---------------------------------------------------------------------------
// SshTransportConfig defaults
// ---------------------------------------------------------------------------

TEST(SshTransportConfig, Defaults) {
    SshTransportConfig cfg;
    EXPECT_EQ(cfg.port, 22);
    EXPECT_EQ(cfg.pty_cols, 80);
    EXPECT_EQ(cfg.pty_rows, 24);
    EXPECT_EQ(cfg.keepalive_seconds, 30);
    EXPECT_EQ(cfg.connect_timeout_seconds, 15);
    EXPECT_EQ(cfg.term_type, "xterm-256color");
    EXPECT_TRUE(cfg.try_agent);
    EXPECT_TRUE(cfg.hostname.empty());
    EXPECT_TRUE(cfg.username.empty());
    EXPECT_TRUE(cfg.password.empty());
    EXPECT_TRUE(cfg.identity_files.empty());
}

// ===========================================================================
// SshKnownHosts unit tests
// ===========================================================================

TEST(SshKnownHosts, CheckNotFoundOnEmpty) {
    TempFileHelper2 tmp;
    SshKnownHosts kh(tmp.path());

    auto result = kh.check("example.com", 22, "ssh-ed25519", "AAAA1234");
    EXPECT_EQ(result, KnownHostResult::NotFound);
}

TEST(SshKnownHosts, AddAndCheckMatch) {
    TempFileHelper2 tmp;
    SshKnownHosts kh(tmp.path());

    EXPECT_TRUE(kh.addEntry("example.com", 22, "ssh-ed25519", "AAAA1234"));

    auto result = kh.check("example.com", 22, "ssh-ed25519", "AAAA1234");
    EXPECT_EQ(result, KnownHostResult::Match);
}

TEST(SshKnownHosts, CheckMismatch) {
    TempFileHelper2 tmp;
    SshKnownHosts kh(tmp.path());

    EXPECT_TRUE(kh.addEntry("example.com", 22, "ssh-ed25519", "AAAA1234"));

    auto result = kh.check("example.com", 22, "ssh-ed25519", "BBBB5678");
    EXPECT_EQ(result, KnownHostResult::Mismatch);
}

TEST(SshKnownHosts, DifferentHostsAreIndependent) {
    TempFileHelper2 tmp;
    SshKnownHosts kh(tmp.path());

    kh.addEntry("host1.com", 22, "ssh-rsa", "KEY1");
    kh.addEntry("host2.com", 22, "ssh-rsa", "KEY2");

    EXPECT_EQ(kh.check("host1.com", 22, "ssh-rsa", "KEY1"), KnownHostResult::Match);
    EXPECT_EQ(kh.check("host2.com", 22, "ssh-rsa", "KEY2"), KnownHostResult::Match);
    EXPECT_EQ(kh.check("host3.com", 22, "ssh-rsa", "KEY3"), KnownHostResult::NotFound);
}

TEST(SshKnownHosts, NonStandardPortUseBrackets) {
    TempFileHelper2 tmp;
    SshKnownHosts kh(tmp.path());

    kh.addEntry("example.com", 2222, "ssh-ed25519", "KEY123");

    EXPECT_EQ(kh.check("example.com", 22, "ssh-ed25519", "KEY123"),
              KnownHostResult::NotFound);

    EXPECT_EQ(kh.check("example.com", 2222, "ssh-ed25519", "KEY123"),
              KnownHostResult::Match);
}

TEST(SshKnownHosts, FilePathIsSet) {
    TempFileHelper2 tmp;
    SshKnownHosts kh(tmp.path());
    EXPECT_EQ(kh.filePath(), tmp.path());
}

TEST(SshKnownHosts, DefaultPathContainsKnownHosts) {
    SshKnownHosts kh;
    auto path = kh.filePath();
    EXPECT_NE(path.find("known_hosts"), std::string::npos);
}

TEST(SshKnownHosts, MultipleEntriesPersist) {
    TempFileHelper2 tmp;
    SshKnownHosts kh(tmp.path());

    for (int i = 0; i < 10; ++i) {
        std::string host = "host" + std::to_string(i) + ".example.com";
        std::string key = "KEY" + std::to_string(i);
        EXPECT_TRUE(kh.addEntry(host, 22, "ssh-rsa", key));
    }

    for (int i = 0; i < 10; ++i) {
        std::string host = "host" + std::to_string(i) + ".example.com";
        std::string key = "KEY" + std::to_string(i);
        EXPECT_EQ(kh.check(host, 22, "ssh-rsa", key), KnownHostResult::Match);
    }
}
