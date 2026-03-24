#if defined(_WIN32)

#include "D3DTextRendererImpl.h"

#include <algorithm>
#include <string>

namespace termcore {

void D3DTextRenderer::Impl::buildProfileDropdownOverlay(float cellW, float cellH,
                                                          float ascent,
                                                          float fontSize) {
    const auto& pd = this->profileDropdown;
    if (!pd.visible || pd.items.empty() ||
        !fontCollection || !rasterizer || !glyphCache || !glyphAtlas)
        return;

    // Layout constants
    float paletteW = (std::min)(viewportWidth * 0.5f, cellW * 40.0f);
    paletteW = (std::max)(paletteW, cellW * 25.0f);
    float titleH = cellH * 1.6f;
    float itemH = cellH * 1.5f;
    int itemCount = static_cast<int>(pd.items.size());
    int maxItems = (std::min)(itemCount, 9);
    float listH = maxItems * itemH;
    float totalH = titleH + listH;
    float paletteX = (viewportWidth - paletteW) / 2.0f;
    float paletteY = viewportHeight * 0.15f;
    float padX = cellW * 1.0f;

    // Semi-transparent backdrop
    {
        D3DCellInstance dim = {};
        dim.position[0] = 0;
        dim.position[1] = 0;
        dim.atlas_size[0] = viewportWidth;
        dim.atlas_size[1] = viewportHeight;
        dim.bg_color[0] = 0.0f; dim.bg_color[1] = 0.0f;
        dim.bg_color[2] = 0.0f; dim.bg_color[3] = 0.4f;
        dim.flags = 8;
        cellInstances.push_back(dim);
    }

    // Shadow
    {
        D3DCellInstance shadow = {};
        shadow.position[0] = paletteX + 4.0f;
        shadow.position[1] = paletteY + 4.0f;
        shadow.atlas_size[0] = paletteW;
        shadow.atlas_size[1] = totalH;
        shadow.bg_color[0] = 0.0f; shadow.bg_color[1] = 0.0f;
        shadow.bg_color[2] = 0.0f; shadow.bg_color[3] = 0.5f;
        shadow.flags = 8;
        cellInstances.push_back(shadow);
    }

    // Background
    {
        D3DCellInstance bg = {};
        bg.position[0] = paletteX;
        bg.position[1] = paletteY;
        bg.atlas_size[0] = paletteW;
        bg.atlas_size[1] = totalH;
        colorFromRGBA(pd.bg_color | 0xFF000000, bg.bg_color);
        bg.flags = 4;
        cellInstances.push_back(bg);
    }

    // Helper: render text string
    auto renderText = [&](const std::string& text, float startX, float startY,
                          uint32_t color, float maxX) {
        float curX = startX;
        for (size_t i = 0; i < text.size(); ++i) {
            if (curX + cellW > maxX) break;
            char32_t cp_ = static_cast<char32_t>(static_cast<unsigned char>(text[i]));
            if (cp_ == ' ') { curX += cellW; continue; }

            CollectionFaceId faceId = fontCollection->resolveFace(cp_);
            if (faceId == kInvalidCollectionFace) { curX += cellW; continue; }
            FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);
            uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp_);
            if (glyphIdx == 0) { curX += cellW; continue; }

            GlyphKey key{rastFace, glyphIdx, {0, 0}};
            auto info = glyphCache->getOrRasterize(key, fontSize, *rasterizer, *glyphAtlas);
            if (!info || info->region.width <= 0) { curX += cellW; continue; }

            D3DCellInstance inst = {};
            inst.position[0] = curX + info->region.bearing_x;
            inst.position[1] = startY + ascent - info->region.bearing_y;
            inst.atlas_uv[0] = static_cast<float>(info->region.x);
            inst.atlas_uv[1] = static_cast<float>(info->region.y);
            inst.atlas_size[0] = static_cast<float>(info->region.width);
            inst.atlas_size[1] = static_cast<float>(info->region.height);
            colorFromRGBA(color | 0xFF000000, inst.fg_color);
            inst.flags = 1;
            cellInstances.push_back(inst);
            curX += cellW;
        }
    };

    // Title: "Select Profile"
    {
        float textY = paletteY + (titleH - cellH) * 0.5f;
        float textX = paletteX + padX;
        float maxX = paletteX + paletteW - padX;
        renderText("Select Profile", textX, textY, pd.fg_color, maxX);
    }

    // Separator
    {
        D3DCellInstance sep = {};
        sep.position[0] = paletteX;
        sep.position[1] = paletteY + titleH - 1.0f;
        sep.atlas_size[0] = paletteW;
        sep.atlas_size[1] = 1.0f;
        sep.bg_color[0] = 1.0f; sep.bg_color[1] = 1.0f;
        sep.bg_color[2] = 1.0f; sep.bg_color[3] = 0.1f;
        sep.flags = 8;
        cellInstances.push_back(sep);
    }

    // List items
    for (int i = 0; i < maxItems; ++i) {
        const auto& item = pd.items[i];
        float itemY = paletteY + titleH + i * itemH;
        bool isSelected = (i == pd.selectedIndex);

        // Selected background
        if (isSelected) {
            D3DCellInstance selBg = {};
            selBg.position[0] = paletteX;
            selBg.position[1] = itemY;
            selBg.atlas_size[0] = paletteW;
            selBg.atlas_size[1] = itemH;
            colorFromRGBA(pd.selected_bg | 0xFF000000, selBg.bg_color);
            selBg.flags = 4;
            cellInstances.push_back(selBg);
        }

        uint32_t textColor = isSelected ? pd.selected_fg : pd.fg_color;
        float textY = itemY + (itemH - cellH) * 0.5f;
        float textX = paletteX + padX;
        float maxX = paletteX + paletteW - padX;

        // Shortcut number hint (1-9)
        if (i < 9) {
            std::string num = std::to_string(i + 1) + ".";
            uint32_t hintColor = isSelected ? pd.selected_fg : pd.hint_fg;
            renderText(num, textX, textY, hintColor, maxX);
            textX += cellW * 3.0f;
        }

        // Profile name
        renderText(item.name, textX, textY, textColor, maxX);
    }
}

} // namespace termcore

#endif // _WIN32
