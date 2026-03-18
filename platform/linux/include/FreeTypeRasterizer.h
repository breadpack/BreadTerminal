#ifndef TERMCORE_FREETYPE_RASTERIZER_H
#define TERMCORE_FREETYPE_RASTERIZER_H

#include "termcore/font/i_font_rasterizer.h"
#include <memory>

namespace termcore {
std::unique_ptr<IFontRasterizer> createFreeTypeRasterizer();
} // namespace termcore
#endif
