#ifndef TERMCORE_CONFIG_FIELD_REGISTRY_H
#define TERMCORE_CONFIG_FIELD_REGISTRY_H

#include "termcore/config.h"
#include "termcore/config_diff.h"
#include <cstddef>
#include <string>

namespace termcore {

// ---- Field descriptor structs (pointer-to-member based) ----

struct StringFieldDesc {
    const char* key;
    std::string Config::*member;
    ConfigDirtyFlags dirty;
};

struct FloatFieldDesc {
    const char* key;
    float Config::*member;
    ConfigDirtyFlags dirty;
};

struct IntFieldDesc {
    const char* key;
    int Config::*member;
    ConfigDirtyFlags dirty;
};

struct BoolFieldDesc {
    const char* key;
    bool Config::*member;
    ConfigDirtyFlags dirty;
};

struct ColorFieldDesc {
    const char* key;
    uint32_t Config::*member;
    ConfigDirtyFlags dirty;
};

// ---- Field tables (defined in config_field_registry.cpp) ----

extern const StringFieldDesc kStringFields[];
extern const size_t kStringFieldCount;

extern const FloatFieldDesc kFloatFields[];
extern const size_t kFloatFieldCount;

extern const IntFieldDesc kIntFields[];
extern const size_t kIntFieldCount;

extern const BoolFieldDesc kBoolFields[];
extern const size_t kBoolFieldCount;

extern const ColorFieldDesc kColorFields[];
extern const size_t kColorFieldCount;

// ---- Convenience lookup by key ----

const StringFieldDesc* findStringField(const std::string& key);
const FloatFieldDesc*  findFloatField(const std::string& key);
const IntFieldDesc*    findIntField(const std::string& key);
const BoolFieldDesc*   findBoolField(const std::string& key);
const ColorFieldDesc*  findColorField(const std::string& key);

// ---- Generic helpers ----

/// Diff all registry fields between two configs, returning combined dirty flags.
/// Complex types (palette, keybindings, font_features) are NOT covered —
/// callers must handle those separately.
ConfigDirtyFlags diffRegistryFields(const Config& old_cfg, const Config& new_cfg);

} // namespace termcore
#endif
