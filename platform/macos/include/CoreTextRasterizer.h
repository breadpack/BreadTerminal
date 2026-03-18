#ifndef TERMCORE_CORETEXT_RASTERIZER_H
#define TERMCORE_CORETEXT_RASTERIZER_H

#include "termcore/font/i_font_rasterizer.h"
#include <memory>

namespace termcore {

/// Create a Core Text based rasterizer (macOS only).
std::unique_ptr<IFontRasterizer> createCoreTextRasterizer();

} // namespace termcore
#endif
