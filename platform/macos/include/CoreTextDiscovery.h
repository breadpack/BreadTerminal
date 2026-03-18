#ifndef TERMCORE_CORETEXT_DISCOVERY_H
#define TERMCORE_CORETEXT_DISCOVERY_H

#include "termcore/font/i_font_discovery.h"
#include <memory>

namespace termcore {

/// Create a Core Text based font discovery (macOS only).
std::unique_ptr<IFontDiscovery> createCoreTextDiscovery();

} // namespace termcore
#endif
