#include <gtest/gtest.h>

#include "termcore/ssh_mux.h"
#include "termcore/ssh_mux_manager.h"
#include "termcore/ssh_transport.h"
#include "termcore/ssh_known_hosts.h"
#include "termcore/mux.h"

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

class TempFileHelper {
public:
    explicit TempFileHelper(const std::string& content = "") {
#if defined(_WIN32)
        char tmpPath[MAX_PATH];
        char tmpFile[MAX_PATH];
        GetTempPathA(MAX_PATH, tmpPath);
        GetTempFileNameA(tmpPath, "ssh", 0, tmpFile);
        path_ = tmpFile;
#else
        char tmpl[] = "/tmp/ssh_mux_test_XXXXXX";
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

    ~TempFileHelper() {
        if (!path_.empty()) std::remove(path_.c_str());
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// ---------------------------------------------------------------------------
// SshMuxSession::makeSessionKey
// ---------------------------------------------------------------------------

TEST(SshMuxSessionKey, BasicKey) {
    SshConfig cfg;
    cfg.host = "myserver";
    cfg.hostname = "192.168.1.10";
    cfg.port = 22;
    cfg.user = "alice";

    EXPECT_EQ(SshMuxSession::makeSessionKey(cfg), "alice@192.168.1.10:22");
}

TEST(SshMuxSessionKey, FallsBackToHostAlias) {
    SshConfig cfg;
    cfg.host = "myalias";
    cfg.port = 2222;
    cfg.user = "bob";

    EXPECT_EQ(SshMuxSession::makeSessionKey(cfg), "bob@myalias:2222");
}

TEST(SshMuxSessionKey, DefaultUserWhenEmpty) {
    SshConfig cfg;
    cfg.hostname = "example.com";
    cfg.port = 22;

    EXPECT_EQ(SshMuxSession::makeSessionKey(cfg), "default@example.com:22");
}

TEST(SshMuxSessionKey, SameConfigProducesSameKey) {
    SshConfig a;
    a.hostname = "server.io";
    a.port = 22;
    a.user = "deploy";

    SshConfig b;
    b.hostname = "server.io";
    b.port = 22;
    b.user = "deploy";
    b.identity_file = "/different/key";  // unrelated field

    EXPECT_EQ(SshMuxSession::makeSessionKey(a),
              SshMuxSession::makeSessionKey(b));
}

// ---------------------------------------------------------------------------
// Channel open / close lifecycle
// ---------------------------------------------------------------------------

TEST(SshMuxSession, OpenChannelReturnsPositiveId) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    int id = session.openChannel();
    EXPECT_GT(id, 0);
    EXPECT_TRUE(session.isConnected());
}

TEST(SshMuxSession, ChannelIdsAreUnique) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    int id1 = session.openChannel();
    int id2 = session.openChannel();
    int id3 = session.openChannel();

    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
    EXPECT_NE(id1, id3);
}

TEST(SshMuxSession, CloseChannelSucceeds) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    int id = session.openChannel();
    EXPECT_TRUE(session.closeChannel(id));
    EXPECT_EQ(session.activeChannelCount(), 0);
}

TEST(SshMuxSession, CloseNonexistentChannelReturnsFalse) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_FALSE(session.closeChannel(999));
}

TEST(SshMuxSession, ListChannelsReflectsState) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_TRUE(session.listChannels().empty());

    int id1 = session.openChannel();
    int id2 = session.openChannel();

    auto channels = session.listChannels();
    EXPECT_EQ(channels.size(), 2u);

    session.closeChannel(id1);
    channels = session.listChannels();
    EXPECT_EQ(channels.size(), 1u);
    EXPECT_EQ(channels[0].channel_id, id2);
}

TEST(SshMuxSession, ActiveChannelCount) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_EQ(session.activeChannelCount(), 0);
    session.openChannel();
    session.openChannel();
    EXPECT_EQ(session.activeChannelCount(), 2);
}

// ---------------------------------------------------------------------------
// Read / write stubs
// ---------------------------------------------------------------------------

TEST(SshMuxSession, WriteToChannelBuffers) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    int id = session.openChannel();
    EXPECT_TRUE(session.writeToChannel(id, "hello"));
    EXPECT_TRUE(session.writeToChannel(id, " world"));
}

TEST(SshMuxSession, WriteToInvalidChannelFails) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_FALSE(session.writeToChannel(42, "data"));
}

TEST(SshMuxSession, ReadFromChannelReturnsEmpty) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    int id = session.openChannel();
    // Stub has no incoming data, so read returns empty.
    EXPECT_TRUE(session.readFromChannel(id).empty());
}

TEST(SshMuxSession, ReadFromInvalidChannelReturnsEmpty) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_TRUE(session.readFromChannel(42).empty());
}

// ---------------------------------------------------------------------------
// Transport state
// ---------------------------------------------------------------------------

TEST(SshMuxSession, TransportStateIsAuthenticatedForStub) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    // Stub sessions report Authenticated since they're always "connected"
    EXPECT_EQ(session.transportState(), SshTransportState::Authenticated);
}

TEST(SshMuxSession, ConnectSucceedsForStub) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_TRUE(session.connect());
    EXPECT_TRUE(session.isConnected());
}

TEST(SshMuxSession, PollIsNoOpForStub) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    // Should not crash
    session.poll();
    EXPECT_TRUE(session.isConnected());
}

TEST(SshMuxSession, LastErrorEmptyForStub) {
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_TRUE(session.lastError().empty());
}

// ---------------------------------------------------------------------------
// Connection sharing: two channels on the same session
// ---------------------------------------------------------------------------

TEST(SshMuxSession, MultipleChannelsShareConnection) {
    SshConfig cfg;
    cfg.hostname = "shared-host";
    cfg.user = "user";
    SshMuxSession session(cfg);

    int ch1 = session.openChannel();
    int ch2 = session.openChannel();

    EXPECT_EQ(session.activeChannelCount(), 2);
    EXPECT_TRUE(session.isConnected());

    EXPECT_TRUE(session.writeToChannel(ch1, "for-ch1"));
    EXPECT_TRUE(session.writeToChannel(ch2, "for-ch2"));

    session.closeChannel(ch1);
    EXPECT_EQ(session.activeChannelCount(), 1);
    EXPECT_TRUE(session.writeToChannel(ch2, "still-alive"));
    EXPECT_FALSE(session.writeToChannel(ch1, "should-fail"));
}

// ---------------------------------------------------------------------------
// SshMuxManager
// ---------------------------------------------------------------------------

TEST(SshMuxManager, GetOrCreateReturnsSameSession) {
    SshMuxManager mgr;

    SshConfig cfg;
    cfg.hostname = "remote.example.com";
    cfg.port = 22;
    cfg.user = "admin";

    auto s1 = mgr.getOrCreateSession(cfg);
    auto s2 = mgr.getOrCreateSession(cfg);

    EXPECT_EQ(s1.get(), s2.get());
    EXPECT_EQ(mgr.sessionCount(), 1u);
}

TEST(SshMuxManager, DifferentHostsGetDifferentSessions) {
    SshMuxManager mgr;

    SshConfig cfgA;
    cfgA.hostname = "hostA";
    cfgA.user = "user";

    SshConfig cfgB;
    cfgB.hostname = "hostB";
    cfgB.user = "user";

    auto sA = mgr.getOrCreateSession(cfgA);
    auto sB = mgr.getOrCreateSession(cfgB);

    EXPECT_NE(sA.get(), sB.get());
    EXPECT_EQ(mgr.sessionCount(), 2u);
}

TEST(SshMuxManager, CloseSessionRemovesIt) {
    SshMuxManager mgr;

    SshConfig cfg;
    cfg.hostname = "host";
    cfg.user = "user";

    auto session = mgr.getOrCreateSession(cfg);
    session->openChannel();

    std::string key = SshMuxSession::makeSessionKey(cfg);
    EXPECT_TRUE(mgr.closeSession(key));
    EXPECT_EQ(mgr.sessionCount(), 0u);
}

TEST(SshMuxManager, CloseNonexistentSessionReturnsFalse) {
    SshMuxManager mgr;
    EXPECT_FALSE(mgr.closeSession("no-such-key"));
}

TEST(SshMuxManager, ListSessionsShowsActiveSessions) {
    SshMuxManager mgr;

    SshConfig cfg;
    cfg.hostname = "myhost";
    cfg.user = "me";

    auto session = mgr.getOrCreateSession(cfg);
    session->openChannel();
    session->openChannel();

    auto list = mgr.listSessions();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].channel_count, 2);
    EXPECT_EQ(list[0].key, SshMuxSession::makeSessionKey(cfg));
}

TEST(SshMuxManager, CleanupRemovesEmptySessions) {
    SshMuxManager mgr;

    SshConfig cfg;
    cfg.hostname = "host";
    cfg.user = "user";

    auto session = mgr.getOrCreateSession(cfg);
    int ch = session->openChannel();
    EXPECT_EQ(mgr.sessionCount(), 1u);

    session->closeChannel(ch);
    mgr.cleanup();
    EXPECT_EQ(mgr.sessionCount(), 0u);
}

TEST(SshMuxManager, CleanupKeepsActiveSessions) {
    SshMuxManager mgr;

    SshConfig cfgA;
    cfgA.hostname = "hostA";
    cfgA.user = "user";

    SshConfig cfgB;
    cfgB.hostname = "hostB";
    cfgB.user = "user";

    auto sA = mgr.getOrCreateSession(cfgA);
    auto sB = mgr.getOrCreateSession(cfgB);

    sA->openChannel();
    int chB = sB->openChannel();
    sB->closeChannel(chB);

    mgr.cleanup();
    EXPECT_EQ(mgr.sessionCount(), 1u);

    auto list = mgr.listSessions();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].key, SshMuxSession::makeSessionKey(cfgA));
}

// ---------------------------------------------------------------------------
// Mux SSH pane integration
// ---------------------------------------------------------------------------

TEST(MuxSshIntegration, AddAndQuerySshPane) {
    SshConfig cfg;
    cfg.hostname = "remote";
    cfg.user = "user";

    auto session = std::make_shared<SshMuxSession>(cfg);
    int ch = session->openChannel();

    Mux mux;
    PaneId nextPaneId = 1;
    mux.setPaneCallbacks(
        [&](int, int) -> PaneId { return nextPaneId++; },
        [](PaneId) {});

    auto ws = mux.createWorkspace("test");
    auto tab = mux.createTab(ws);
    auto panes = mux.allPanes(ws, tab);
    ASSERT_FALSE(panes.empty());

    PaneId pane = panes[0];
    mux.addSshPane(pane, ch, session);

    EXPECT_TRUE(mux.isSshPane(pane));
    EXPECT_EQ(mux.sshChannelForPane(pane), ch);
    EXPECT_EQ(mux.sshSessionForPane(pane).get(), session.get());
}

TEST(MuxSshIntegration, NonSshPaneReturnsFalse) {
    Mux mux;
    PaneId nextPaneId = 1;
    mux.setPaneCallbacks(
        [&](int, int) -> PaneId { return nextPaneId++; },
        [](PaneId) {});

    auto ws = mux.createWorkspace("test");
    auto tab = mux.createTab(ws);
    auto panes = mux.allPanes(ws, tab);
    ASSERT_FALSE(panes.empty());

    EXPECT_FALSE(mux.isSshPane(panes[0]));
    EXPECT_EQ(mux.sshChannelForPane(panes[0]), -1);
    EXPECT_EQ(mux.sshSessionForPane(panes[0]), nullptr);
}

TEST(MuxSshIntegration, RemoveSshPaneClears) {
    SshConfig cfg;
    cfg.hostname = "remote";
    auto session = std::make_shared<SshMuxSession>(cfg);
    int ch = session->openChannel();

    Mux mux;
    PaneId nextPaneId = 1;
    mux.setPaneCallbacks(
        [&](int, int) -> PaneId { return nextPaneId++; },
        [](PaneId) {});

    auto ws = mux.createWorkspace("test");
    auto tab = mux.createTab(ws);
    auto panes = mux.allPanes(ws, tab);
    PaneId pane = panes[0];

    mux.addSshPane(pane, ch, session);
    EXPECT_TRUE(mux.isSshPane(pane));

    mux.removeSshPane(pane);
    EXPECT_FALSE(mux.isSshPane(pane));
}

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
    // Should not start with ~ anymore (assuming HOME is set)
    EXPECT_NE(result[0], '~');
    // Should end with the rest of the path
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
    // We can't assert specific files exist, but the function should work
    // without crashing. On CI there may be no keys.
    for (const auto& f : files) {
        EXPECT_FALSE(f.empty());
        EXPECT_NE(f[0], '~');  // should be expanded
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
    TempFileHelper tmp;
    SshKnownHosts kh(tmp.path());

    auto result = kh.check("example.com", 22, "ssh-ed25519", "AAAA1234");
    EXPECT_EQ(result, KnownHostResult::NotFound);
}

TEST(SshKnownHosts, AddAndCheckMatch) {
    TempFileHelper tmp;
    SshKnownHosts kh(tmp.path());

    EXPECT_TRUE(kh.addEntry("example.com", 22, "ssh-ed25519", "AAAA1234"));

    auto result = kh.check("example.com", 22, "ssh-ed25519", "AAAA1234");
    EXPECT_EQ(result, KnownHostResult::Match);
}

TEST(SshKnownHosts, CheckMismatch) {
    TempFileHelper tmp;
    SshKnownHosts kh(tmp.path());

    EXPECT_TRUE(kh.addEntry("example.com", 22, "ssh-ed25519", "AAAA1234"));

    // Different key for the same host
    auto result = kh.check("example.com", 22, "ssh-ed25519", "BBBB5678");
    EXPECT_EQ(result, KnownHostResult::Mismatch);
}

TEST(SshKnownHosts, DifferentHostsAreIndependent) {
    TempFileHelper tmp;
    SshKnownHosts kh(tmp.path());

    kh.addEntry("host1.com", 22, "ssh-rsa", "KEY1");
    kh.addEntry("host2.com", 22, "ssh-rsa", "KEY2");

    EXPECT_EQ(kh.check("host1.com", 22, "ssh-rsa", "KEY1"), KnownHostResult::Match);
    EXPECT_EQ(kh.check("host2.com", 22, "ssh-rsa", "KEY2"), KnownHostResult::Match);
    EXPECT_EQ(kh.check("host3.com", 22, "ssh-rsa", "KEY3"), KnownHostResult::NotFound);
}

TEST(SshKnownHosts, NonStandardPortUseBrackets) {
    TempFileHelper tmp;
    SshKnownHosts kh(tmp.path());

    kh.addEntry("example.com", 2222, "ssh-ed25519", "KEY123");

    // Same host, different port should not match
    EXPECT_EQ(kh.check("example.com", 22, "ssh-ed25519", "KEY123"),
              KnownHostResult::NotFound);

    // Same host, same non-standard port should match
    EXPECT_EQ(kh.check("example.com", 2222, "ssh-ed25519", "KEY123"),
              KnownHostResult::Match);
}

TEST(SshKnownHosts, FilePathIsSet) {
    TempFileHelper tmp;
    SshKnownHosts kh(tmp.path());
    EXPECT_EQ(kh.filePath(), tmp.path());
}

TEST(SshKnownHosts, DefaultPathContainsKnownHosts) {
    SshKnownHosts kh;  // default path
    auto path = kh.filePath();
    EXPECT_NE(path.find("known_hosts"), std::string::npos);
}

TEST(SshKnownHosts, MultipleEntriesPersist) {
    TempFileHelper tmp;
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
