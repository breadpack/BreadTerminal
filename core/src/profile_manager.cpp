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

} // namespace termcore
