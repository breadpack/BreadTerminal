#include "termcore/update_checker.h"
#include "termcore/notification.h"

#include <gtest/gtest.h>

using namespace termcore;

// ---------------------------------------------------------------------------
// Version comparison tests
// ---------------------------------------------------------------------------

TEST(CompareVersions, Equal) {
    EXPECT_EQ(compareVersions("1.0.0", "1.0.0"), 0);
    EXPECT_EQ(compareVersions("0.1.0", "0.1.0"), 0);
    EXPECT_EQ(compareVersions("2.3.4", "2.3.4"), 0);
}

TEST(CompareVersions, MajorDifference) {
    EXPECT_EQ(compareVersions("1.0.0", "2.0.0"), -1);
    EXPECT_EQ(compareVersions("2.0.0", "1.0.0"), 1);
}

TEST(CompareVersions, MinorDifference) {
    EXPECT_EQ(compareVersions("1.0.0", "1.1.0"), -1);
    EXPECT_EQ(compareVersions("1.2.0", "1.1.0"), 1);
}

TEST(CompareVersions, PatchDifference) {
    EXPECT_EQ(compareVersions("1.0.0", "1.0.1"), -1);
    EXPECT_EQ(compareVersions("1.0.2", "1.0.1"), 1);
}

TEST(CompareVersions, DoubleDigitVersions) {
    EXPECT_EQ(compareVersions("1.9.0", "1.10.0"), -1);
    EXPECT_EQ(compareVersions("1.10.0", "1.9.0"), 1);
    EXPECT_EQ(compareVersions("1.10.0", "1.10.0"), 0);
}

TEST(CompareVersions, DifferentLengths) {
    EXPECT_EQ(compareVersions("1.0", "1.0.0"), 0);
    EXPECT_EQ(compareVersions("1.0.0", "1.0"), 0);
    EXPECT_EQ(compareVersions("1.0", "1.0.1"), -1);
}

TEST(CompareVersions, PreRelease) {
    // Pre-release is less than release
    EXPECT_EQ(compareVersions("1.0.0-beta", "1.0.0"), -1);
    EXPECT_EQ(compareVersions("1.0.0", "1.0.0-beta"), 1);

    // Pre-release compared to pre-release (lexicographic)
    EXPECT_EQ(compareVersions("1.0.0-alpha", "1.0.0-beta"), -1);
    EXPECT_EQ(compareVersions("1.0.0-beta", "1.0.0-alpha"), 1);
    EXPECT_EQ(compareVersions("1.0.0-beta", "1.0.0-beta"), 0);
}

TEST(CompareVersions, PreReleaseVsHigherVersion) {
    // 1.0.0-beta < 1.0.1 (different numeric parts)
    EXPECT_EQ(compareVersions("1.0.0-beta", "1.0.1"), -1);
    EXPECT_EQ(compareVersions("1.1.0", "1.0.0-beta"), 1);
}

TEST(CompareVersions, LeadingV) {
    EXPECT_EQ(compareVersions("v1.0.0", "1.0.0"), 0);
    EXPECT_EQ(compareVersions("v1.0.0", "v1.0.1"), -1);
    EXPECT_EQ(compareVersions("V2.0.0", "1.0.0"), 1);
}

// ---------------------------------------------------------------------------
// Manifest parsing tests
// ---------------------------------------------------------------------------

TEST(ParseManifest, ValidFull) {
    std::string json = R"({
        "version": "1.2.0",
        "url": "https://example.com/release/1.2.0",
        "notes": "Bug fixes and improvements",
        "sha256": "abcdef1234567890"
    })";

    UpdateManifest m;
    EXPECT_TRUE(parseUpdateManifest(json, m));
    EXPECT_EQ(m.version, "1.2.0");
    EXPECT_EQ(m.url, "https://example.com/release/1.2.0");
    EXPECT_EQ(m.notes, "Bug fixes and improvements");
    EXPECT_EQ(m.sha256, "abcdef1234567890");
}

TEST(ParseManifest, VersionOnly) {
    std::string json = R"({"version": "2.0.0"})";

    UpdateManifest m;
    EXPECT_TRUE(parseUpdateManifest(json, m));
    EXPECT_EQ(m.version, "2.0.0");
    EXPECT_TRUE(m.url.empty());
    EXPECT_TRUE(m.notes.empty());
    EXPECT_TRUE(m.sha256.empty());
}

TEST(ParseManifest, MissingVersion) {
    std::string json = R"({"url": "https://example.com"})";

    UpdateManifest m;
    EXPECT_FALSE(parseUpdateManifest(json, m));
}

TEST(ParseManifest, InvalidJson) {
    UpdateManifest m;
    EXPECT_FALSE(parseUpdateManifest("not json at all", m));
    EXPECT_FALSE(parseUpdateManifest("", m));
    EXPECT_FALSE(parseUpdateManifest("{broken", m));
}

TEST(ParseManifest, VersionNotString) {
    std::string json = R"({"version": 123})";

    UpdateManifest m;
    EXPECT_FALSE(parseUpdateManifest(json, m));
}

// ---------------------------------------------------------------------------
// UpdateChecker tests
// ---------------------------------------------------------------------------

TEST(UpdateChecker, DefaultState) {
    UpdateChecker checker;
    EXPECT_FALSE(checker.isUpdateAvailable());
    EXPECT_TRUE(checker.latestVersion().empty());
    EXPECT_TRUE(checker.releaseUrl().empty());
    EXPECT_TRUE(checker.releaseNotes().empty());
    EXPECT_EQ(checker.checkInterval(), 24);
    EXPECT_EQ(checker.manifestUrl(),
              "https://breadterminal.dev/api/v1/updates/latest");
}

TEST(UpdateChecker, SetCheckInterval) {
    UpdateChecker checker;
    checker.setCheckInterval(12);
    EXPECT_EQ(checker.checkInterval(), 12);

    // Invalid values should be ignored
    checker.setCheckInterval(0);
    EXPECT_EQ(checker.checkInterval(), 12);
    checker.setCheckInterval(-1);
    EXPECT_EQ(checker.checkInterval(), 12);
}

TEST(UpdateChecker, SetManifestUrl) {
    UpdateChecker checker;
    checker.setManifestUrl("https://custom.dev/updates");
    EXPECT_EQ(checker.manifestUrl(), "https://custom.dev/updates");
}

TEST(UpdateChecker, DetectsNewerVersion) {
    UpdateChecker checker;
    // Current version is 0.1.0 (from termcore_version())
    std::string json = R"({"version": "1.0.0", "url": "https://example.com"})";
    checker.setManifestData(json);

    EXPECT_TRUE(checker.isUpdateAvailable());
    EXPECT_EQ(checker.latestVersion(), "1.0.0");
    EXPECT_EQ(checker.releaseUrl(), "https://example.com");
}

TEST(UpdateChecker, IgnoresOlderVersion) {
    UpdateChecker checker;
    // Current version is 0.1.0
    std::string json = R"({"version": "0.0.1"})";
    checker.setManifestData(json);

    EXPECT_FALSE(checker.isUpdateAvailable());
}

TEST(UpdateChecker, IgnoresSameVersion) {
    UpdateChecker checker;
    std::string current = UpdateChecker::currentVersion();
    std::string json = R"({"version": ")" + current + R"("})";
    checker.setManifestData(json);

    EXPECT_FALSE(checker.isUpdateAvailable());
}

TEST(UpdateChecker, InvalidManifestClearsState) {
    UpdateChecker checker;
    checker.setManifestData("not valid json");
    EXPECT_FALSE(checker.isUpdateAvailable());
}

TEST(UpdateChecker, NotifiesViaNotificationStore) {
    UpdateChecker checker;
    NotificationStore store;

    std::string json = R"({
        "version": "2.0.0",
        "url": "https://breadterminal.dev/releases/2.0.0"
    })";
    checker.setManifestData(json);
    EXPECT_TRUE(checker.isUpdateAvailable());

    checker.notifyIfAvailable(&store);
    EXPECT_EQ(store.count(), 1u);

    const auto& n = store.all().front();
    EXPECT_EQ(n.title, "Update Available");
    EXPECT_NE(n.body.find("v2.0.0"), std::string::npos);
    EXPECT_NE(n.body.find("v0.1.0"), std::string::npos);
    EXPECT_EQ(n.urgency, NotificationUrgency::Normal);
    EXPECT_EQ(n.source, NotificationSource::System);
}

TEST(UpdateChecker, NotifiesOnlyOnce) {
    UpdateChecker checker;
    NotificationStore store;

    checker.setManifestData(R"({"version": "2.0.0"})");
    checker.notifyIfAvailable(&store);
    checker.notifyIfAvailable(&store);
    checker.notifyIfAvailable(&store);

    EXPECT_EQ(store.count(), 1u);
}

TEST(UpdateChecker, NoNotifyWhenNoUpdate) {
    UpdateChecker checker;
    NotificationStore store;

    checker.setManifestData(R"({"version": "0.0.1"})");
    checker.notifyIfAvailable(&store);

    EXPECT_EQ(store.count(), 0u);
}

TEST(UpdateChecker, NoNotifyWithNullStore) {
    UpdateChecker checker;
    checker.setManifestData(R"({"version": "2.0.0"})");
    // Should not crash
    checker.notifyIfAvailable(nullptr);
}

TEST(UpdateChecker, ShouldCheckWithNoConfigDir) {
    UpdateChecker checker;
    // No config dir set — should always return true
    EXPECT_TRUE(checker.shouldCheck());
}
