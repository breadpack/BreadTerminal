#pragma once
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// A stored credential entry
struct PasswordEntry {
    std::string id;           // unique identifier (UUID-like)
    std::string label;        // user-visible label (e.g., "prod-server", "database")
    std::string username;     // optional username
    std::string account;      // account/host identifier
    std::chrono::system_clock::time_point lastUsed;
    // Note: actual password is NOT stored in memory, only loaded on demand
};

/// Password Manager - stores and retrieves credentials
/// Passwords are stored in an encrypted file using a master password
class PasswordManager {
public:
    PasswordManager();
    ~PasswordManager();

    /// Initialize with storage path
    void setStoragePath(const std::string& path);
    std::string storagePath() const;

    /// Lock/unlock with master password
    bool unlock(const std::string& masterPassword);
    void lock();
    bool isUnlocked() const;

    /// CRUD operations (require unlocked state)
    bool addEntry(const std::string& label, const std::string& username,
                  const std::string& account, const std::string& password);
    bool removeEntry(const std::string& id);
    bool updatePassword(const std::string& id, const std::string& newPassword);

    /// Retrieve password (requires unlocked state)
    /// Returns empty string if not found or locked
    std::string getPassword(const std::string& id) const;

    /// List entries (without passwords)
    std::vector<PasswordEntry> listEntries() const;

    /// Search entries by label or account
    std::vector<PasswordEntry> searchEntries(const std::string& query) const;

    /// Auto-lock after timeout (seconds, 0 = never)
    void setAutoLockTimeout(int seconds);
    int autoLockTimeout() const;

    /// Check if should auto-lock based on last activity
    bool shouldAutoLock() const;
    void touchActivity();

    /// Check if storage file exists
    bool hasStorage() const;

    /// Create new storage with master password
    bool createStorage(const std::string& masterPassword);

    /// Entry count
    size_t entryCount() const;

private:
    std::string storagePath_;
    bool unlocked_ = false;
    int autoLockTimeout_ = 300; // 5 minutes default
    std::chrono::steady_clock::time_point lastActivity_;
    std::vector<PasswordEntry> entries_;

    // In a real implementation, this would use proper encryption
    // For now, we use a simple XOR-based obfuscation as a placeholder
    std::string masterKey_;

    // Map of entry ID -> encrypted password (loaded from file)
    std::unordered_map<std::string, std::string> encryptedPasswords_;

    bool loadEntries();
    bool saveEntries();
    std::string encrypt(const std::string& plaintext) const;
    std::string decrypt(const std::string& ciphertext) const;
    std::string generateId() const;
};

} // namespace termcore
