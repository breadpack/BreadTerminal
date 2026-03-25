#include "termcore/provider_registry.h"
#include <algorithm>

namespace termcore {

void ProviderRegistry::registerProvider(ProviderInfo info) {
    for (auto& p : providers_) {
        if (p.id == info.id) { p = std::move(info); return; }
    }
    providers_.push_back(std::move(info));
}

const ProviderInfo* ProviderRegistry::findById(const std::string& id) const {
    for (const auto& p : providers_) { if (p.id == id) return &p; }
    return nullptr;
}

const ProviderInfo* ProviderRegistry::findByAgentType(const std::string& agent_type) const {
    for (const auto& p : providers_) { if (p.agent_type == agent_type) return &p; }
    return nullptr;
}

static bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

const ProviderInfo* ProviderRegistry::detect(const std::string& process_name,
    const std::vector<std::string>& env_vars) const {
    for (const auto& p : providers_) {
        for (const auto& proc : p.detect_process) {
            if (containsIgnoreCase(process_name, proc)) return &p;
        }
        for (const auto& marker : p.detect_env) {
            for (const auto& env : env_vars) {
                if (containsIgnoreCase(env, marker)) return &p;
            }
        }
    }
    return nullptr;
}

void ProviderRegistry::markInstalled(const std::string& provider_id) { installed_.insert(provider_id); }
bool ProviderRegistry::isInstalled(const std::string& provider_id) const { return installed_.count(provider_id) > 0; }

}  // namespace termcore
