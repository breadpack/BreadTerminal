#ifndef TERMCORE_DIRECTWRITE_RASTERIZER_H
#define TERMCORE_DIRECTWRITE_RASTERIZER_H

#include "termcore/font/i_font_rasterizer.h"
#include <memory>

namespace termcore {
std::unique_ptr<IFontRasterizer> createDirectWriteRasterizer();
} // namespace termcore
#endif
