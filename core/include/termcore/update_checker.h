#ifndef TERMCORE_UPDATE_CHECKER_H
#define TERMCORE_UPDATE_CHECKER_H

#include <chrono>
#include <string>

namespace termcore {

class NotificationStore;

/// Parsed update manifest from the update server
struct UpdateManifest {
    std::string version;
    std::string url;
    std::string notes;
    std::string sha256;
};

/// Compare two semantic version strings.
/// Returns -1 if a < b, 0 if a == b, 1 if a > b.
/// Handles versions like "1.2.3", "1.2.3-beta", "1.10.0".
int compareVersions(const std::string& a, const std::string& b);

/// Parse a JSON update manifest string into an UpdateManifest struct.
/// Returns true on success.
bool parseUpdateManifest(const std::string& json, UpdateManifest& out);

/// Checks for new versions and notifies the user via NotificationStore.
/// Does NOT perform HTTP fetches — platform code calls setManifestData()
/// after fetching the manifest.
class UpdateChecker {
public:
    /// Default manifest URL
    static constexpr const char* kDefaultManifestUrl =
        "https://breadterminal.dev/api/v1/updates/latest";

    UpdateChecker();

    /// Set the manifest URL (default: kDefaultManifestUrl)
    void setManifestUrl(const std::string& url) { manifest_url_ = url; }

    /// Get the manifest URL
    const std::string& manifestUrl() const { return manifest_url_; }

    /// Set how often to check (in hours). Default: 24.
    void setCheckInterval(int hours);

    /// Get the check interval in hours
    int checkInterval() const { return check_interval_hours_; }

    /// Returns true if enough time has elapsed since the last check
    bool shouldCheck() const;

    /// Record that a check was performed now.
    /// Persists the timestamp to the config directory.
    void markChecked();

    /// Feed the fetched manifest JSON. Platform code calls this after
    /// performing the HTTP GET to manifestUrl().
    /// If a newer version is found, stores the result internally.
    void setManifestData(const std::string& json);

    /// Notify the user if an update is available.
    /// Call this after setManifestData(). Posts to the NotificationStore
    /// if one is set and an update was found.
    void notifyIfAvailable(NotificationStore* store);

    /// True if setManifestData() found a newer version
    bool isUpdateAvailable() const { return update_available_; }

    /// The latest version string from the manifest (empty if no update)
    const std::string& latestVersion() const { return manifest_.version; }

    /// Release URL from the manifest
    const std::string& releaseUrl() const { return manifest_.url; }

    /// Release notes from the manifest
    const std::string& releaseNotes() const { return manifest_.notes; }

    /// Set/get the config directory for persisting the last-check timestamp.
    /// If not set, shouldCheck() always returns true and markChecked() is a no-op.
    void setConfigDir(const std::string& dir) { config_dir_ = dir; }

    /// Get the current version string
    static std::string currentVersion();

private:
    std::string manifest_url_;
    int check_interval_hours_ = 24;
    std::string config_dir_;
    bool update_available_ = false;
    bool already_notified_ = false;
    UpdateManifest manifest_;

    /// Load last check timestamp from file
    std::chrono::system_clock::time_point loadLastCheckTime() const;

    /// Save timestamp to file
    void saveLastCheckTime(std::chrono::system_clock::time_point tp) const;

    /// Path to the timestamp persistence file
    std::string timestampFilePath() const;
};

} // namespace termcore

#endif // TERMCORE_UPDATE_CHECKER_H
