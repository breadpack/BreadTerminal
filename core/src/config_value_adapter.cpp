#include "termcore/config_value_adapter.h"
#include "termcore/config_field_registry.h"

#include <string>

namespace termcore {

// ---------------------------------------------------------------------------
// String getters / setters
// ---------------------------------------------------------------------------

std::string getConfigString(const Config& cfg, const std::string& key) {
    if (auto* f = findStringField(key)) return cfg.*f->member;
    return {};
}

void setConfigString(Config& cfg, const std::string& key, const std::string& val) {
    if (auto* f = findStringField(key)) cfg.*f->member = val;
}

// ---------------------------------------------------------------------------
// Int getters / setters
// ---------------------------------------------------------------------------

int getConfigInt(const Config& cfg, const std::string& key) {
    if (auto* f = findIntField(key)) return cfg.*f->member;
    return 0;
}

void setConfigInt(Config& cfg, const std::string& key, int val) {
    if (auto* f = findIntField(key)) cfg.*f->member = val;
}

// ---------------------------------------------------------------------------
// Float getters / setters
// ---------------------------------------------------------------------------

float getConfigFloat(const Config& cfg, const std::string& key) {
    if (auto* f = findFloatField(key)) return cfg.*f->member;
    return 0.0f;
}

void setConfigFloat(Config& cfg, const std::string& key, float val) {
    if (auto* f = findFloatField(key)) cfg.*f->member = val;
}

// ---------------------------------------------------------------------------
// Bool getters / setters
// ---------------------------------------------------------------------------

bool getConfigBool(const Config& cfg, const std::string& key) {
    if (auto* f = findBoolField(key)) return cfg.*f->member;
    return false;
}

void setConfigBool(Config& cfg, const std::string& key, bool val) {
    if (auto* f = findBoolField(key)) cfg.*f->member = val;
}

// ---------------------------------------------------------------------------
// Color getters / setters
// ---------------------------------------------------------------------------

uint32_t getConfigColor(const Config& cfg, const std::string& key) {
    if (auto* f = findColorField(key)) return cfg.*f->member;
    // palette colors: "palette_0" through "palette_15"
    if (key.rfind("palette_", 0) == 0) {
        int idx = std::stoi(key.substr(8));
        if (idx >= 0 && idx < 16) return cfg.palette[idx];
    }
    return 0;
}

void setConfigColor(Config& cfg, const std::string& key, uint32_t val) {
    if (auto* f = findColorField(key)) { cfg.*f->member = val; return; }
    if (key.rfind("palette_", 0) == 0) {
        int idx = std::stoi(key.substr(8));
        if (idx >= 0 && idx < 16) cfg.palette[idx] = val;
    }
}

} // namespace termcore
