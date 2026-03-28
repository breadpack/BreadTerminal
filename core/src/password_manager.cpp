#include "termcore/password_manager.h"
#include "termcore/base64.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace termcore {

namespace fs = std::filesystem;
using json = nlohmann::json;

// Static counter for ID generation
static uint64_t sIdCounter = 0;

PasswordManager::PasswordManager()
    : lastActivity_(std::chrono::steady_clock::now()) {}

PasswordManager::~PasswordManager() {
    lock();
}

void PasswordManager::setStoragePath(const std::string& path) {
    storagePath_ = path;
}

std::string PasswordManager::storagePath() const {
    return storagePath_;
}

bool PasswordManager::unlock(const std::string& masterPassword) {
    if (storagePath_.empty() || !hasStorage()) return false;

    masterKey_ = masterPassword;
    if (!loadEntries()) {
        masterKey_.clear();
        return false;
    }

    unlocked_ = true;
    touchActivity();
    return true;
}

void PasswordManager::lock() {
    unlocked_ = false;
    masterKey_.clear();
    entries_.clear();
    encryptedPasswords_.clear();
}

bool PasswordManager::isUnlocked() const {
    return unlocked_;
}

bool PasswordManager::addEntry(const std::string& label,
                               const std::string& username,
                               const std::string& account,
                               const std::string& password) {
    if (!unlocked_) return false;

    PasswordEntry entry;
    entry.id = generateId();
    entry.label = label;
    entry.username = username;
    entry.account = account;
    entry.lastUsed = std::chrono::system_clock::now();

    entries_.push_back(entry);
    encryptedPasswords_[entry.id] = encrypt(password);

    touchActivity();
    return saveEntries();
}

bool PasswordManager::removeEntry(const std::string& id) {
    if (!unlocked_) return false;

    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const PasswordEntry& e) { return e.id == id; });
    if (it == entries_.end()) return false;

    entries_.erase(it);
    encryptedPasswords_.erase(id);

    touchActivity();
    return saveEntries();
}

bool PasswordManager::updatePassword(const std::string& id,
                                     const std::string& newPassword) {
    if (!unlocked_) return false;

    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const PasswordEntry& e) { return e.id == id; });
    if (it == entries_.end()) return false;

    encryptedPasswords_[id] = encrypt(newPassword);

    touchActivity();
    return saveEntries();
}

std::string PasswordManager::getPassword(const std::string& id) const {
    if (!unlocked_) return "";

    auto it = encryptedPasswords_.find(id);
    if (it == encryptedPasswords_.end()) return "";

    return decrypt(it->second);
}

std::vector<PasswordEntry> PasswordManager::listEntries() const {
    if (!unlocked_) return {};
    return entries_;
}

std::vector<PasswordEntry> PasswordManager::searchEntries(
    const std::string& query) const {
    if (!unlocked_) return {};

    // Case-insensitive substring match on label and account
    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::vector<PasswordEntry> results;
    for (const auto& entry : entries_) {
        std::string lowerLabel = entry.label;
        std::transform(lowerLabel.begin(), lowerLabel.end(),
                       lowerLabel.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        std::string lowerAccount = entry.account;
        std::transform(lowerAccount.begin(), lowerAccount.end(),
                       lowerAccount.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (lowerLabel.find(lowerQuery) != std::string::npos ||
            lowerAccount.find(lowerQuery) != std::string::npos) {
            results.push_back(entry);
        }
    }
    return results;
}

void PasswordManager::setAutoLockTimeout(int seconds) {
    autoLockTimeout_ = seconds;
}

int PasswordManager::autoLockTimeout() const {
    return autoLockTimeout_;
}

bool PasswordManager::shouldAutoLock() const {
    if (!unlocked_ || autoLockTimeout_ <= 0) return false;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       now - lastActivity_)
                       .count();
    return elapsed >= autoLockTimeout_;
}

void PasswordManager::touchActivity() {
    lastActivity_ = std::chrono::steady_clock::now();
}

bool PasswordManager::hasStorage() const {
    if (storagePath_.empty()) return false;
    return fs::exists(storagePath_);
}

bool PasswordManager::createStorage(const std::string& masterPassword) {
    if (storagePath_.empty()) return false;

    // Create parent directories if needed
    auto parent = fs::path(storagePath_).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) return false;
    }

    masterKey_ = masterPassword;
    entries_.clear();
    encryptedPasswords_.clear();

    bool ok = saveEntries();
    if (!ok) {
        masterKey_.clear();
        return false;
    }

    unlocked_ = true;
    touchActivity();
    return true;
}

size_t PasswordManager::entryCount() const {
    return entries_.size();
}

bool PasswordManager::loadEntries() {
    std::ifstream file(storagePath_);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    // The file stores base64-encoded encrypted JSON
    std::string decrypted = decrypt(base64DecodeToString(content));

    json j;
    try {
        j = json::parse(decrypted);
    } catch (...) {
        // Decryption with wrong key produces garbage that won't parse
        return false;
    }

    // Validate magic marker
    if (!j.contains("magic") || j["magic"] != "breadterminal-passwords") {
        return false;
    }

    entries_.clear();
    encryptedPasswords_.clear();

    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& ej : j["entries"]) {
            PasswordEntry entry;
            entry.id = ej.value("id", "");
            entry.label = ej.value("label", "");
            entry.username = ej.value("username", "");
            entry.account = ej.value("account", "");

            if (ej.contains("lastUsed")) {
                auto ms = ej["lastUsed"].get<int64_t>();
                entry.lastUsed = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(ms));
            }

            // Passwords are stored encrypted within the already-encrypted file
            // for an extra layer (so they're not in plaintext even in decrypted
            // JSON)
            if (ej.contains("password")) {
                encryptedPasswords_[entry.id] = ej["password"].get<std::string>();
            }

            entries_.push_back(entry);
        }
    }

    return true;
}

bool PasswordManager::saveEntries() {
    json j;
    j["magic"] = "breadterminal-passwords";
    j["version"] = 1;

    json entriesJson = json::array();
    for (const auto& entry : entries_) {
        json ej;
        ej["id"] = entry.id;
        ej["label"] = entry.label;
        ej["username"] = entry.username;
        ej["account"] = entry.account;
        ej["lastUsed"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                             entry.lastUsed.time_since_epoch())
                             .count();

        auto pit = encryptedPasswords_.find(entry.id);
        if (pit != encryptedPasswords_.end()) {
            ej["password"] = pit->second;
        }

        entriesJson.push_back(ej);
    }
    j["entries"] = entriesJson;

    std::string plaintext = j.dump();
    std::string encrypted = base64Encode(encrypt(plaintext));

    std::ofstream file(storagePath_, std::ios::trunc);
    if (!file.is_open()) return false;

    file << encrypted;
    file.close();
    return file.good() || !file.fail();
}

// XOR-based obfuscation placeholder
// NOTE: In production, this should use AES-256-GCM with a key derived from
// the master password via Argon2 or PBKDF2. XOR is NOT cryptographically
// secure and is used here only as a structural placeholder.
std::string PasswordManager::encrypt(const std::string& plaintext) const {
    if (masterKey_.empty()) return plaintext;

    std::string result = plaintext;
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] ^= masterKey_[i % masterKey_.size()];
    }
    return result;
}

std::string PasswordManager::decrypt(const std::string& ciphertext) const {
    // XOR encryption is symmetric
    return encrypt(ciphertext);
}

std::string PasswordManager::generateId() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch())
                  .count();

    ++sIdCounter;

    std::ostringstream oss;
    oss << std::hex << ms << "-" << sIdCounter;
    return oss.str();
}

} // namespace termcore
