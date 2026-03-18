#ifndef TERMCORE_DIRECTWRITE_DISCOVERY_H
#define TERMCORE_DIRECTWRITE_DISCOVERY_H

#include "termcore/font/i_font_discovery.h"
#include <memory>

namespace termcore {
std::unique_ptr<IFontDiscovery> createDirectWriteDiscovery();
} // namespace termcore
#endif
