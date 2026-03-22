#include <gtest/gtest.h>
#include "termcore/password_manager.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace termcore;
namespace fs = std::filesystem;

class PasswordManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a temporary file for storage
        tmpPath_ = (fs::temp_directory_path() / "breadterm_pw_test.enc").string();
        mgr_.setStoragePath(tmpPath_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(tmpPath_, ec);
    }

    std::string tmpPath_;
    PasswordManager mgr_;
};

// 1. createStorage and hasStorage
TEST_F(PasswordManagerTest, CreateStorageAndHasStorage) {
    EXPECT_FALSE(mgr_.hasStorage());
    EXPECT_TRUE(mgr_.createStorage("master123"));
    EXPECT_TRUE(mgr_.hasStorage());
}

// 2. Unlock/lock cycle
TEST_F(PasswordManagerTest, UnlockLockCycle) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.lock();

    EXPECT_FALSE(mgr_.isUnlocked());
    EXPECT_TRUE(mgr_.unlock("master123"));
    EXPECT_TRUE(mgr_.isUnlocked());

    mgr_.lock();
    EXPECT_FALSE(mgr_.isUnlocked());
}

// 3. Unlock with wrong password fails
TEST_F(PasswordManagerTest, UnlockWrongPassword) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.lock();

    EXPECT_FALSE(mgr_.unlock("wrong_password"));
    EXPECT_FALSE(mgr_.isUnlocked());
}

// 4. addEntry and listEntries
TEST_F(PasswordManagerTest, AddEntryAndListEntries) {
    ASSERT_TRUE(mgr_.createStorage("master123"));

    EXPECT_TRUE(mgr_.addEntry("prod-server", "admin", "192.168.1.1", "s3cret"));
    EXPECT_TRUE(mgr_.addEntry("database", "root", "db.local", "dbpass"));

    auto entries = mgr_.listEntries();
    EXPECT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].label, "prod-server");
    EXPECT_EQ(entries[0].username, "admin");
    EXPECT_EQ(entries[0].account, "192.168.1.1");
    EXPECT_EQ(entries[1].label, "database");
}

// 5. searchEntries - case-insensitive
TEST_F(PasswordManagerTest, SearchEntries) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.addEntry("Prod-Server", "admin", "192.168.1.1", "s3cret");
    mgr_.addEntry("Database", "root", "db.local", "dbpass");
    mgr_.addEntry("Staging", "deploy", "staging.example.com", "stage123");

    // Search by label (case-insensitive)
    auto results = mgr_.searchEntries("prod");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].label, "Prod-Server");

    // Search by account
    results = mgr_.searchEntries("local");
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].label, "Database");

    // Search matching nothing
    results = mgr_.searchEntries("nonexistent");
    EXPECT_TRUE(results.empty());
}

// 6. getPassword returns correct password
TEST_F(PasswordManagerTest, GetPasswordReturnsCorrect) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.addEntry("server", "admin", "host", "my_password_123");

    auto entries = mgr_.listEntries();
    ASSERT_EQ(entries.size(), 1u);

    std::string pw = mgr_.getPassword(entries[0].id);
    EXPECT_EQ(pw, "my_password_123");
}

// 7. getPassword persists across lock/unlock
TEST_F(PasswordManagerTest, PasswordPersistsAcrossLockUnlock) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.addEntry("server", "admin", "host", "persistent_pw");

    auto entries = mgr_.listEntries();
    ASSERT_EQ(entries.size(), 1u);
    std::string id = entries[0].id;

    mgr_.lock();
    ASSERT_TRUE(mgr_.unlock("master123"));

    EXPECT_EQ(mgr_.getPassword(id), "persistent_pw");
}

// 8. removeEntry
TEST_F(PasswordManagerTest, RemoveEntry) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.addEntry("entry1", "user1", "acc1", "pw1");
    mgr_.addEntry("entry2", "user2", "acc2", "pw2");

    auto entries = mgr_.listEntries();
    ASSERT_EQ(entries.size(), 2u);

    EXPECT_TRUE(mgr_.removeEntry(entries[0].id));
    entries = mgr_.listEntries();
    EXPECT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].label, "entry2");
}

// 9. removeEntry with invalid ID returns false
TEST_F(PasswordManagerTest, RemoveEntryInvalidId) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    EXPECT_FALSE(mgr_.removeEntry("nonexistent-id"));
}

// 10. Operations fail when locked
TEST_F(PasswordManagerTest, OperationsFailWhenLocked) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.lock();

    EXPECT_FALSE(mgr_.addEntry("label", "user", "acc", "pw"));
    EXPECT_FALSE(mgr_.removeEntry("some-id"));
    EXPECT_FALSE(mgr_.updatePassword("some-id", "newpw"));
    EXPECT_EQ(mgr_.getPassword("some-id"), "");
    EXPECT_TRUE(mgr_.listEntries().empty());
    EXPECT_TRUE(mgr_.searchEntries("query").empty());
}

// 11. autoLock timeout
TEST_F(PasswordManagerTest, AutoLockTimeout) {
    ASSERT_TRUE(mgr_.createStorage("master123"));

    // Default timeout is 300 seconds
    EXPECT_EQ(mgr_.autoLockTimeout(), 300);
    EXPECT_FALSE(mgr_.shouldAutoLock());

    // Set very short timeout
    mgr_.setAutoLockTimeout(1);
    EXPECT_EQ(mgr_.autoLockTimeout(), 1);

    // Should not auto-lock immediately after activity
    mgr_.touchActivity();
    EXPECT_FALSE(mgr_.shouldAutoLock());

    // Wait just over 1 second
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    EXPECT_TRUE(mgr_.shouldAutoLock());
}

// 12. autoLock disabled with 0
TEST_F(PasswordManagerTest, AutoLockDisabledWithZero) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.setAutoLockTimeout(0);
    EXPECT_FALSE(mgr_.shouldAutoLock());
}

// 13. entryCount
TEST_F(PasswordManagerTest, EntryCount) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    EXPECT_EQ(mgr_.entryCount(), 0u);

    mgr_.addEntry("a", "u", "h", "p");
    EXPECT_EQ(mgr_.entryCount(), 1u);

    mgr_.addEntry("b", "u", "h", "p");
    EXPECT_EQ(mgr_.entryCount(), 2u);
}

// 14. updatePassword
TEST_F(PasswordManagerTest, UpdatePassword) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    mgr_.addEntry("server", "admin", "host", "old_password");

    auto entries = mgr_.listEntries();
    ASSERT_EQ(entries.size(), 1u);
    std::string id = entries[0].id;

    EXPECT_TRUE(mgr_.updatePassword(id, "new_password"));
    EXPECT_EQ(mgr_.getPassword(id), "new_password");
}

// 15. updatePassword with invalid ID returns false
TEST_F(PasswordManagerTest, UpdatePasswordInvalidId) {
    ASSERT_TRUE(mgr_.createStorage("master123"));
    EXPECT_FALSE(mgr_.updatePassword("nonexistent", "pw"));
}

// 16. storagePath getter/setter
TEST_F(PasswordManagerTest, StoragePath) {
    PasswordManager pm;
    EXPECT_TRUE(pm.storagePath().empty());

    pm.setStoragePath("/some/path/file.enc");
    EXPECT_EQ(pm.storagePath(), "/some/path/file.enc");
}

// 17. unlock without storage fails
TEST_F(PasswordManagerTest, UnlockWithoutStorageFails) {
    PasswordManager pm;
    pm.setStoragePath("/nonexistent/path/file.enc");
    EXPECT_FALSE(pm.unlock("password"));
}
