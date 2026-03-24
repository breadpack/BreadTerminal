#include "termcore/font_manager.h"
#include <algorithm>

namespace termcore {

FontManager::FontManager(FontCollection* fc, const std::string& family, float baseSize)
    : fc_(fc)
    , family_(family)
    , baseSize_(baseSize)
    , currentSize_(baseSize)
{
    refreshMetrics();
}

void FontManager::changeFontSize(float delta) {
    float newSize = currentSize_ + delta;
    newSize = (std::max)(kMinFontSize, (std::min)(kMaxFontSize, newSize));
    if (newSize == currentSize_) return;

    currentSize_ = newSize;
    if (fc_) {
        fc_->setPrimaryFont(family_, currentSize_);
        for (const auto& fb : fallbackFamilies_) {
            fc_->addFallbackFont(fb);
        }
    }
    refreshMetrics();
}

void FontManager::resetFontSize() {
    if (currentSize_ == baseSize_) return;

    currentSize_ = baseSize_;
    if (fc_) {
        fc_->setPrimaryFont(family_, currentSize_);
        for (const auto& fb : fallbackFamilies_) {
            fc_->addFallbackFont(fb);
        }
    }
    refreshMetrics();
}

void FontManager::setFont(const std::string& family, float size) {
    family_ = family;
    baseSize_ = size;
    currentSize_ = size;
    if (fc_) {
        fc_->setPrimaryFont(family_, currentSize_);
        for (const auto& fb : fallbackFamilies_) {
            fc_->addFallbackFont(fb);
        }
    }
    refreshMetrics();
}

void FontManager::setFallbackFonts(const std::vector<std::string>& families) {
    fallbackFamilies_ = families;
    // Re-apply: setPrimaryFont clears chain, then re-add fallbacks
    if (fc_) {
        fc_->setPrimaryFont(family_, currentSize_);
        for (const auto& fb : fallbackFamilies_) {
            fc_->addFallbackFont(fb);
        }
    }
    refreshMetrics();
}

void FontManager::refreshMetrics() {
    if (!fc_) return;
    auto metrics = fc_->primaryMetrics();
    cellW_ = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
    cellH_ = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;
}

void FontManager::recalcGrid(int viewportW, int viewportH,
                             int& outRows, int& outCols) const
{
    outCols = (cellW_ > 0) ? static_cast<int>(viewportW / cellW_) : 80;
    outRows = (cellH_ > 0) ? static_cast<int>(viewportH / cellH_) : 24;
    if (outCols < 1) outCols = 1;
    if (outRows < 1) outRows = 1;
}

} // namespace termcore
