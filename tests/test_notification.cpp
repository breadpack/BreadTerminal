#include "termcore/notification.h"

#include <gtest/gtest.h>

using namespace termcore;

TEST(NotificationStore, EmptyStore) {
    NotificationStore store;
    EXPECT_EQ(store.count(), 0u);
    EXPECT_EQ(store.unreadCount(), 0u);
    EXPECT_TRUE(store.all().empty());
}

TEST(NotificationStore, AddNotification) {
    NotificationStore store;
    uint64_t id = store.add(1, NotificationSource::Agent,
                            NotificationUrgency::Normal, "Title", "Body");
    EXPECT_EQ(store.count(), 1u);
    EXPECT_EQ(store.unreadCount(), 1u);
    EXPECT_EQ(id, 1u);

    const auto& n = store.all().front();
    EXPECT_EQ(n.title, "Title");
    EXPECT_EQ(n.body, "Body");
    EXPECT_EQ(n.pane_id, 1u);
    EXPECT_EQ(n.source, NotificationSource::Agent);
    EXPECT_EQ(n.urgency, NotificationUrgency::Normal);
    EXPECT_FALSE(n.read);
}

TEST(NotificationStore, MarkRead) {
    NotificationStore store;
    uint64_t id = store.add(1, NotificationSource::Agent,
                            NotificationUrgency::Normal, "T", "B");
    EXPECT_EQ(store.unreadCount(), 1u);

    store.markRead(id);
    EXPECT_EQ(store.unreadCount(), 0u);
    EXPECT_TRUE(store.all().front().read);
}

TEST(NotificationStore, MarkAllRead) {
    NotificationStore store;
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "A",
              "1");
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "B",
              "2");
    store.add(2, NotificationSource::System, NotificationUrgency::Low, "C",
              "3");
    EXPECT_EQ(store.unreadCount(), 3u);

    store.markAllRead();
    EXPECT_EQ(store.unreadCount(), 0u);
    for (const auto& n : store.all()) {
        EXPECT_TRUE(n.read);
    }
}

TEST(NotificationStore, Remove) {
    NotificationStore store;
    uint64_t id1 = store.add(1, NotificationSource::Agent,
                             NotificationUrgency::Normal, "A", "1");
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "B",
              "2");
    EXPECT_EQ(store.count(), 2u);

    store.remove(id1);
    EXPECT_EQ(store.count(), 1u);
    EXPECT_EQ(store.all().front().title, "B");
}

TEST(NotificationStore, Clear) {
    NotificationStore store;
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "A",
              "1");
    store.add(2, NotificationSource::System, NotificationUrgency::Low, "B",
              "2");
    EXPECT_EQ(store.count(), 2u);

    store.clear();
    EXPECT_EQ(store.count(), 0u);
}

TEST(NotificationStore, ClearForPane) {
    NotificationStore store;
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "A",
              "1");
    store.add(2, NotificationSource::System, NotificationUrgency::Low, "B",
              "2");
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "C",
              "3");
    EXPECT_EQ(store.count(), 3u);

    store.clearForPane(1);
    EXPECT_EQ(store.count(), 1u);
    EXPECT_EQ(store.all().front().pane_id, 2u);
}

TEST(NotificationStore, AddFromOsc9) {
    NotificationStore store;
    uint64_t id = store.addFromOsc(1, 9, "Build complete");
    EXPECT_EQ(store.count(), 1u);

    const auto& n = store.all().front();
    EXPECT_EQ(n.source, NotificationSource::OSC9);
    EXPECT_EQ(n.title, "Terminal Notification");
    EXPECT_EQ(n.body, "Build complete");
}

TEST(NotificationStore, AddFromOsc777) {
    NotificationStore store;
    store.addFromOsc(1, 777, "notify;Build Status;Build succeeded");

    const auto& n = store.all().front();
    EXPECT_EQ(n.source, NotificationSource::OSC777);
    EXPECT_EQ(n.title, "Build Status");
    EXPECT_EQ(n.body, "Build succeeded");
}

TEST(NotificationStore, AddFromOsc99) {
    NotificationStore store;
    store.addFromOsc(1, 99, "i=1;d=0:Hello world");

    const auto& n = store.all().front();
    EXPECT_EQ(n.source, NotificationSource::OSC99);
    EXPECT_EQ(n.body, "Hello world");
}

TEST(NotificationStore, AddFromOsc99TitlePayload) {
    NotificationStore store;
    store.addFromOsc(1, 99, "p=title:My Title");

    const auto& n = store.all().front();
    EXPECT_EQ(n.source, NotificationSource::OSC99);
    EXPECT_EQ(n.title, "My Title");
}

TEST(NotificationStore, MaxNotificationsEvictsOldest) {
    NotificationStore store(3);
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "A",
              "1");
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "B",
              "2");
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "C",
              "3");
    EXPECT_EQ(store.count(), 3u);

    // Adding a 4th should evict "A" (oldest, at back)
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "D",
              "4");
    EXPECT_EQ(store.count(), 3u);

    // Newest first: D, C, B
    EXPECT_EQ(store.all()[0].title, "D");
    EXPECT_EQ(store.all()[1].title, "C");
    EXPECT_EQ(store.all()[2].title, "B");
}

TEST(NotificationStore, CallbackFiresOnAdd) {
    NotificationStore store;
    Notification received;
    bool called = false;

    store.setCallback([&](const Notification& n) {
        called = true;
        received = n;
    });

    store.add(5, NotificationSource::System, NotificationUrgency::Critical,
              "Alert", "Something happened");

    EXPECT_TRUE(called);
    EXPECT_EQ(received.title, "Alert");
    EXPECT_EQ(received.body, "Something happened");
    EXPECT_EQ(received.pane_id, 5u);
    EXPECT_EQ(received.urgency, NotificationUrgency::Critical);
}

TEST(NotificationStore, ForPaneFiltersCorrectly) {
    NotificationStore store;
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "A",
              "1");
    store.add(2, NotificationSource::Agent, NotificationUrgency::Normal, "B",
              "2");
    store.add(1, NotificationSource::Agent, NotificationUrgency::Normal, "C",
              "3");

    auto pane1 = store.forPane(1);
    EXPECT_EQ(pane1.size(), 2u);
    // Newest first
    EXPECT_EQ(pane1[0]->title, "C");
    EXPECT_EQ(pane1[1]->title, "A");

    auto pane2 = store.forPane(2);
    EXPECT_EQ(pane2.size(), 1u);
    EXPECT_EQ(pane2[0]->title, "B");

    auto pane3 = store.forPane(3);
    EXPECT_TRUE(pane3.empty());
}

TEST(NotificationStore, HasUnreadCorrectState) {
    NotificationStore store;
    EXPECT_FALSE(store.hasUnread(1));

    uint64_t id = store.add(1, NotificationSource::Agent,
                            NotificationUrgency::Normal, "A", "1");
    EXPECT_TRUE(store.hasUnread(1));
    EXPECT_FALSE(store.hasUnread(2));

    store.markRead(id);
    EXPECT_FALSE(store.hasUnread(1));
}

TEST(NotificationStore, MultiplePanesIndependent) {
    NotificationStore store;
    uint64_t id1 = store.add(1, NotificationSource::Agent,
                             NotificationUrgency::Normal, "A", "1");
    store.add(2, NotificationSource::Agent, NotificationUrgency::Normal, "B",
              "2");

    EXPECT_TRUE(store.hasUnread(1));
    EXPECT_TRUE(store.hasUnread(2));

    store.markRead(id1);
    EXPECT_FALSE(store.hasUnread(1));
    EXPECT_TRUE(store.hasUnread(2));

    store.clearForPane(1);
    EXPECT_EQ(store.count(), 1u);
    EXPECT_EQ(store.forPane(2).size(), 1u);
    EXPECT_TRUE(store.forPane(1).empty());
}
