#ifndef TERMCORE_I_FONT_DISCOVERY_H
#define TERMCORE_I_FONT_DISCOVERY_H

#include "font_metrics.h"
#include <vector>

namespace termcore {

/// Abstract interface for platform-specific font discovery.
/// Implementations: CoreTextDiscovery (macOS), DirectWriteDiscovery (Windows), FontconfigDiscovery (Linux)
class IFontDiscovery {
public:
    virtual ~IFontDiscovery() = default;

    /// Find fonts matching a query. Returns list of matching font descriptors.
    virtual std::vector<FontDescriptor> findFonts(const FontQuery& query) = 0;

    /// Find a fallback font for a specific codepoint and style.
    virtual FontDescriptor findFallback(char32_t codepoint, FontStyle style) = 0;

    /// Get the system default monospace font.
    virtual FontDescriptor defaultMonospace() = 0;
};

} // namespace termcore

#endif
