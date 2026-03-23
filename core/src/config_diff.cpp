#include "termcore/config_diff.h"
#include "termcore/config_field_registry.h"

#include <cstring>

namespace termcore {

ConfigDirtyFlags diffConfig(const Config& old_cfg, const Config& new_cfg) {
    // Registry handles all simple scalar fields
    ConfigDirtyFlags flags = diffRegistryFields(old_cfg, new_cfg);

    // --- Complex types not in registry ---

    // Palette (fixed-size array)
    if (std::memcmp(old_cfg.palette, new_cfg.palette, sizeof(old_cfg.palette)) != 0)
        flags |= ConfigDirtyFlags::Colors;

    // Font features (vector)
    if (old_cfg.font_features != new_cfg.font_features)
        flags |= ConfigDirtyFlags::Font;

    // Keybindings (vector of structs)
    if (old_cfg.keybindings.size() != new_cfg.keybindings.size()) {
        flags |= ConfigDirtyFlags::Keybindings;
    } else {
        for (size_t i = 0; i < old_cfg.keybindings.size(); ++i) {
            if (old_cfg.keybindings[i].trigger != new_cfg.keybindings[i].trigger ||
                old_cfg.keybindings[i].action != new_cfg.keybindings[i].action) {
                flags |= ConfigDirtyFlags::Keybindings;
                break;
            }
        }
    }

    return flags;
}

} // namespace termcore
