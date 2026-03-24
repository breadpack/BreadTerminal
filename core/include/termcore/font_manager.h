#ifndef TERMCORE_FONT_MANAGER_H
#define TERMCORE_FONT_MANAGER_H

#include "termcore/font/font_collection.h"
#include <string>
#include <vector>

namespace termcore {

class FontManager {
public:
    FontManager(FontCollection* fc, const std::string& family, float baseSize);

    void changeFontSize(float delta);
    void resetFontSize();
    void setFont(const std::string& family, float size);
    void setFallbackFonts(const std::vector<std::string>& families);

    float cellWidth() const { return cellW_; }
    float cellHeight() const { return cellH_; }
    float currentFontSize() const { return currentSize_; }
    float baseFontSize() const { return baseSize_; }
    const std::string& fontFamily() const { return family_; }

    // Recalculate grid dimensions from viewport pixel size
    void recalcGrid(int viewportW, int viewportH,
                    int& outRows, int& outCols) const;

private:
    void refreshMetrics();

    FontCollection* fc_;          // not owned - platform manages lifetime
    std::string family_;
    std::vector<std::string> fallbackFamilies_;
    float baseSize_;
    float currentSize_;
    float cellW_ = 8.0f;
    float cellH_ = 16.0f;

    static constexpr float kMinFontSize = 6.0f;
    static constexpr float kMaxFontSize = 72.0f;
};

} // namespace termcore
#endif
