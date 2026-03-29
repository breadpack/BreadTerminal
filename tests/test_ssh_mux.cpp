#include <gtest/gtest.h>

#include "termcore/ssh_mux.h"
#include "termcore/ssh_mux_manager.h"
#include "termcore/ssh_transport.h"
#include "termcore/mux.h"

using namespace termcore;

// When built with real libssh2, channel/session tests need a live SSH server
// which is unavailable in CI.  Skip them and only run the pure-logic tests.
#if TERMCORE_HAS_LIBSSH2
#define SKIP_WITHOUT_STUB() GTEST_SKIP() << "Requires stub SshMuxSession (no libssh2)"
#else
#define SKIP_WITHOUT_STUB() ((void)0)
#endif

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
    b.identity_file = "/different/key";

    EXPECT_EQ(SshMuxSession::makeSessionKey(a),
              SshMuxSession::makeSessionKey(b));
}

// ---------------------------------------------------------------------------
// Channel open / close lifecycle
// ---------------------------------------------------------------------------

TEST(SshMuxSession, OpenChannelReturnsPositiveId) {
    SKIP_WITHOUT_STUB();
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    int id = session.openChannel();
    EXPECT_GT(id, 0);
    EXPECT_TRUE(session.isConnected());
}

TEST(SshMuxSession, ChannelIdsAreUnique) {
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    int id = session.openChannel();
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
    SKIP_WITHOUT_STUB();
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_EQ(session.transportState(), SshTransportState::Authenticated);
}

TEST(SshMuxSession, ConnectSucceedsForStub) {
    SKIP_WITHOUT_STUB();
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_TRUE(session.connect());
    EXPECT_TRUE(session.isConnected());
}

TEST(SshMuxSession, PollIsNoOpForStub) {
    SKIP_WITHOUT_STUB();
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    session.poll();
    EXPECT_TRUE(session.isConnected());
}

TEST(SshMuxSession, LastErrorEmptyForStub) {
    SKIP_WITHOUT_STUB();
    SshConfig cfg;
    cfg.hostname = "host";
    SshMuxSession session(cfg);

    EXPECT_TRUE(session.lastError().empty());
}

// ---------------------------------------------------------------------------
// Connection sharing
// ---------------------------------------------------------------------------

TEST(SshMuxSession, MultipleChannelsShareConnection) {
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
    SKIP_WITHOUT_STUB();
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
