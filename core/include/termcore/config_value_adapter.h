#pragma once

#include "config.h"

#include <cstdint>
#include <string>

namespace termcore {

// Generic getters — return value by key from Config
std::string getConfigString(const Config& cfg, const std::string& key);
int getConfigInt(const Config& cfg, const std::string& key);
float getConfigFloat(const Config& cfg, const std::string& key);
bool getConfigBool(const Config& cfg, const std::string& key);
uint32_t getConfigColor(const Config& cfg, const std::string& key);

// Generic setters — set value by key on Config
void setConfigString(Config& cfg, const std::string& key, const std::string& val);
void setConfigInt(Config& cfg, const std::string& key, int val);
void setConfigFloat(Config& cfg, const std::string& key, float val);
void setConfigBool(Config& cfg, const std::string& key, bool val);
void setConfigColor(Config& cfg, const std::string& key, uint32_t val);

} // namespace termcore
