#ifndef TERMCORE_CONFIG_APPLIER_H
#define TERMCORE_CONFIG_APPLIER_H

#include "termcore/config.h"
#include "termcore/platform_host.h"

namespace termcore {

class TabController;
class FontManager;

class ConfigApplier {
public:
    // Full config update (from settings UI)
    void applyFull(Config& config, const Config& newConfig,
                   TabController& tabs, FontManager& fontMgr,
                   IPlatformHost* host);

    // Color-only update (from theme hub)
    void applyColors(Config& config, const Config& newConfig,
                     TabController& tabs, IPlatformHost* host);

    // Font-only update (from font hub)
    void applyFont(Config& config, const std::string& family,
                   TabController& tabs, FontManager& fontMgr,
                   IPlatformHost* host);

    // Persist current config to disk
    void persist(const Config& config);
};

} // namespace termcore
#endif
