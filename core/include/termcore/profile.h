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

    bool is_default = false;
    bool hidden = false;
    bool auto_detected = false;
};

/// Apply profile appearance overrides on top of global Config.
Config resolveProfileConfig(const Config& global, const Profile& profile);

} // namespace termcore
#endif
