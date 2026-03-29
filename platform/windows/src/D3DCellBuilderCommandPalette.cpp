#if defined(_WIN32)

#include "D3DTextRendererImpl.h"

#include <algorithm>
#include <string>

namespace termcore {

void D3DTextRenderer::Impl::buildCommandPaletteOverlay(float cellW, float cellH,
                                                         float ascent,
                                                         float fontSize) {
    const auto& cp = this->commandPalette;
    if (!cp.visible || !fontCollection || !rasterizer || !glyphCache || !glyphAtlas)
        return;

    // Layout constants
    float paletteW = (std::min)(viewportWidth * cp.width_percent, cellW * 50.0f);
    paletteW = (std::max)(paletteW, cellW * 30.0f);
    float inputH = cellH * 1.6f;
    float itemH = cellH * 1.4f;
    int itemCount = static_cast<int>(cp.items.size());
    int maxItems = (std::min)(itemCount, cp.max_items);
    float listH = maxItems * itemH;
    float totalH = inputH + listH;
    float paletteX = (viewportWidth - paletteW) / 2.0f;
    float paletteY = viewportHeight * 0.15f; // 15% from top, like VS Code
    float padX = cellW * 0.8f;

    // Semi-transparent backdrop (full screen dimming)
    {
        D3DCellInstance dim = {};
        dim.position[0] = 0;
        dim.position[1] = 0;
        dim.atlas_size[0] = viewportWidth;
        dim.atlas_size[1] = viewportHeight;
        dim.bg_color[0] = 0.0f; dim.bg_color[1] = 0.0f;
        dim.bg_color[2] = 0.0f; dim.bg_color[3] = cp.backdrop_opacity;
        dim.flags = 8; // solid color
        cellInstances.push_back(dim);
    }

    // Shadow (offset dark rect behind palette)
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

    // Palette background
    {
        D3DCellInstance bg = {};
        bg.position[0] = paletteX;
        bg.position[1] = paletteY;
        bg.atlas_size[0] = paletteW;
        bg.atlas_size[1] = totalH;
        colorFromRGBA(cp.bg_color | 0xFF000000, bg.bg_color);
        bg.flags = 4; // is_bg
        cellInstances.push_back(bg);
    }

    // Input field background
    {
        float inputMargin = 4.0f;
        D3DCellInstance inputBg = {};
        inputBg.position[0] = paletteX + inputMargin;
        inputBg.position[1] = paletteY + inputMargin;
        inputBg.atlas_size[0] = paletteW - inputMargin * 2;
        inputBg.atlas_size[1] = inputH - inputMargin * 2;
        colorFromRGBA(cp.input_bg_color | 0xFF000000, inputBg.bg_color);
        inputBg.flags = 4;
        cellInstances.push_back(inputBg);
    }

    // Helper lambda to render a text string at a given position
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
            inst.flags = 1; // has_glyph
            cellInstances.push_back(inst);
            curX += cellW;
        }
    };

    // Render the ">" prompt and query text in input field
    {
        float textY = paletteY + (inputH - cellH) * 0.5f;
        float textX = paletteX + padX;
        float maxX = paletteX + paletteW - padX;

        // ">" prompt character
        renderText(">", textX, textY, cp.hint_fg, maxX);
        textX += cellW * 1.5f;

        // Query text
        if (!cp.query.empty()) {
            renderText(cp.query, textX, textY, cp.fg_color, maxX);
        }

        // Cursor bar (blinking indicator after query)
        float cursorX = textX + cp.query.size() * cellW;
        if (cursorX < maxX) {
            D3DCellInstance cursor = {};
            cursor.position[0] = cursorX;
            cursor.position[1] = textY + 2.0f;
            cursor.atlas_size[0] = 2.0f; // thin bar cursor
            cursor.atlas_size[1] = cellH - 4.0f;
            cursor.bg_color[0] = 1.0f; cursor.bg_color[1] = 1.0f;
            cursor.bg_color[2] = 1.0f; cursor.bg_color[3] = 0.8f;
            cursor.flags = 8;
            cellInstances.push_back(cursor);
        }
    }

    // Separator line between input and list
    {
        D3DCellInstance sep = {};
        sep.position[0] = paletteX;
        sep.position[1] = paletteY + inputH - 1.0f;
        sep.atlas_size[0] = paletteW;
        sep.atlas_size[1] = 1.0f;
        sep.bg_color[0] = 1.0f; sep.bg_color[1] = 1.0f;
        sep.bg_color[2] = 1.0f; sep.bg_color[3] = 0.1f;
        sep.flags = 8;
        cellInstances.push_back(sep);
    }

    // Render list items
    for (int i = 0; i < maxItems; ++i) {
        const auto& item = cp.items[i];
        float itemY = paletteY + inputH + i * itemH;
        bool isSelected = (i == cp.selectedIndex);

        // Selected item background
        if (isSelected) {
            D3DCellInstance selBg = {};
            selBg.position[0] = paletteX;
            selBg.position[1] = itemY;
            selBg.atlas_size[0] = paletteW;
            selBg.atlas_size[1] = itemH;
            colorFromRGBA(cp.selected_bg | 0xFF000000, selBg.bg_color);
            selBg.flags = 4;
            cellInstances.push_back(selBg);
        }

        uint32_t textColor = isSelected ? cp.selected_fg : cp.fg_color;
        float textY = itemY + (itemH - cellH) * 0.5f;
        float textX = paletteX + padX;
        float maxX = paletteX + paletteW - padX;

        // Command name
        renderText(item.name, textX, textY, textColor, maxX - cellW * 12);

        // Shortcut hint (right-aligned)
        if (!item.shortcut_hint.empty()) {
            float hintW = item.shortcut_hint.size() * cellW;
            float hintX = paletteX + paletteW - padX - hintW;
            uint32_t hintColor = isSelected ? cp.selected_fg : cp.hint_fg;
            renderText(item.shortcut_hint, hintX, textY, hintColor, maxX);
        }
    }
}

} // namespace termcore

#endif // _WIN32
