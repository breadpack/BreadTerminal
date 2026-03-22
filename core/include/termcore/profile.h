#ifndef TERMCORE_PROFILE_H
#define TERMCORE_PROFILE_H

#include <optional>
#include <string>
#include <vector>

namespace termcore {

struct Config;  // forward declaration — defined in config.h

struct Profile {
    std::string id;
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::string working_dir;
    std::string icon;

    std::optional<std::string> theme;
    std::optional<std::string> font_family;
    std::optional<float> font_size;
    std::optional<std::string> cursor_style;

    bool hidden = false;
    bool auto_detected = false;
};

/// Apply profile appearance overrides on top of global Config.
Config resolveProfileConfig(const Config& global, const Profile& profile);

class ProfileManager {
public:
    ProfileManager();

    const std::vector<Profile>& allProfiles() const;
    std::vector<const Profile*> visibleProfiles() const;
    const Profile& defaultProfile() const;
    const Profile* findProfile(const std::string& id) const;

    void setProfile(const Profile& profile);
    void setDefaultProfile(const std::string& id);
    void hideProfile(const std::string& id);
    void setDetectedProfiles(std::vector<Profile> detected);

private:
    std::vector<Profile> detected_;
    std::vector<Profile> user_;
    std::vector<Profile> merged_;
    std::string default_id_;
    std::vector<std::string> hidden_ids_;
    Profile fallback_;

    void rebuildMerged();
    void ensureFallback();
};

class ShellDetector {
public:
    static std::vector<Profile> detect();
};

} // namespace termcore
#endif
