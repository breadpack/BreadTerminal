#include "termcore/profile.h"
#include "termcore/config.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <string>

namespace termcore {

Config resolveProfileConfig(const Config& global, const Profile& profile) {
    Config resolved = global;  // copy all fields
    if (profile.theme.has_value())        resolved.theme = *profile.theme;
    if (profile.font_family.has_value())  resolved.font_family = *profile.font_family;
    if (profile.font_size.has_value())    resolved.font_size = *profile.font_size;
    if (profile.cursor_style.has_value()) resolved.cursor_style = *profile.cursor_style;
    return resolved;
}

ProfileManager::ProfileManager() {
    ensureFallback();
}

void ProfileManager::ensureFallback() {
    fallback_.id = "__fallback__";
#if defined(_WIN32)
    char sys[MAX_PATH] = {};
    GetSystemDirectoryA(sys, MAX_PATH);
    fallback_.command = std::string(sys) + "\\cmd.exe";
    fallback_.name = "cmd.exe";
    fallback_.icon = "cmd";
#else
    const char* sh = std::getenv("SHELL");
    fallback_.command = (sh && sh[0]) ? sh : "/bin/sh";
    std::string cmd = fallback_.command;
    auto pos = cmd.rfind('/');
    fallback_.name = (pos != std::string::npos) ? cmd.substr(pos + 1) : cmd;
    fallback_.icon = fallback_.name;
#endif
}

const std::vector<Profile>& ProfileManager::allProfiles() const { return merged_; }

std::vector<const Profile*> ProfileManager::visibleProfiles() const {
    std::vector<const Profile*> result;
    for (const auto& p : merged_) {
        if (!p.hidden) result.push_back(&p);
    }
    return result;
}

const Profile& ProfileManager::defaultProfile() const {
    if (!default_id_.empty()) {
        for (const auto& p : merged_) {
            if (p.id == default_id_) return p;
        }
    }
    if (!merged_.empty()) return merged_[0];
    return fallback_;
}

const Profile* ProfileManager::findProfile(const std::string& id) const {
    for (const auto& p : merged_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void ProfileManager::setProfile(const Profile& profile) {
    for (auto& p : user_) {
        if (p.id == profile.id) {
            p = profile;
            rebuildMerged();
            return;
        }
    }
    user_.push_back(profile);
    rebuildMerged();
}

void ProfileManager::setDefaultProfile(const std::string& id) { default_id_ = id; }

void ProfileManager::hideProfile(const std::string& id) {
    if (std::find(hidden_ids_.begin(), hidden_ids_.end(), id) == hidden_ids_.end()) {
        hidden_ids_.push_back(id);
    }
    rebuildMerged();
}

void ProfileManager::setDetectedProfiles(std::vector<Profile> detected) {
    detected_ = std::move(detected);
    rebuildMerged();
}

void ProfileManager::rebuildMerged() {
    merged_.clear();
    merged_ = detected_;

    for (const auto& user : user_) {
        bool found = false;
        for (auto& m : merged_) {
            if (m.id == user.id) {
                if (!user.name.empty())       m.name = user.name;
                if (!user.command.empty())     m.command = user.command;
                if (!user.args.empty())        m.args = user.args;
                if (!user.working_dir.empty()) m.working_dir = user.working_dir;
                if (!user.icon.empty())        m.icon = user.icon;
                if (user.theme.has_value())       m.theme = user.theme;
                if (user.font_family.has_value()) m.font_family = user.font_family;
                if (user.font_size.has_value())   m.font_size = user.font_size;
                if (user.cursor_style.has_value()) m.cursor_style = user.cursor_style;
                m.auto_detected = false;
                found = true;
                break;
            }
        }
        if (!found) merged_.push_back(user);
    }

    for (auto& m : merged_) {
        m.hidden = std::find(hidden_ids_.begin(), hidden_ids_.end(), m.id) != hidden_ids_.end();
    }
}

} // namespace termcore
