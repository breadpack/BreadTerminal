#import "MetalTextRendererImpl.h"

#include <string>
#include <algorithm>

namespace termcore {

// Decode first UTF-8 codepoint from a string
static char32_t firstCodepoint(const std::string& s) {
    if (s.empty()) return 0;
    auto b = static_cast<unsigned char>(s[0]);
    if (b < 0x80) return b;
    char32_t cp = 0;
    int extra = 0;
    if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; }
    else return 0;
    if (static_cast<int>(s.size()) < extra + 1) return 0;
    for (int i = 1; i <= extra; ++i) {
        auto c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (c & 0x3F);
    }
    return cp;
}

// Helper: create a CellInstance for an overlay solid-color rect at absolute pixel position.
static CellInstance makeOverlayRect(float x, float y, float w, float h,
                                     uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    CellInstance inst = {};
    inst.grid_col = static_cast<uint16_t>(x);
    inst.grid_row = static_cast<uint16_t>(y);
    inst.glyph_width = static_cast<uint16_t>(w);
    inst.glyph_height = static_cast<uint16_t>(h);
    inst.bg_r = r;
    inst.bg_g = g;
    inst.bg_b = b;
    inst.bg_a = a;
    inst.flags = 8;  // overlay mode
    return inst;
}

// Helper: create overlay rect from uint32_t RGB color
static CellInstance makeOverlayRectRGB(float x, float y, float w, float h,
                                        uint32_t color, uint8_t alpha = 255) {
    return makeOverlayRect(x, y, w, h,
                           (color >> 16) & 0xFF,
                           (color >> 8) & 0xFF,
                           color & 0xFF,
                           alpha);
}

// Helper: create a CellInstance for an overlay glyph at absolute pixel position.
static CellInstance makeOverlayGlyph(float x, float y,
                                      const GlyphInfo& info,
                                      uint32_t color) {
    CellInstance inst = {};
    inst.grid_col = static_cast<uint16_t>(x);
    inst.grid_row = static_cast<uint16_t>(y);
    inst.glyph_x = static_cast<uint16_t>(info.region.x);
    inst.glyph_y = static_cast<uint16_t>(info.region.y);
    inst.glyph_width = static_cast<uint16_t>(info.region.width);
    inst.glyph_height = static_cast<uint16_t>(info.region.height);
    inst.offset_x = static_cast<int16_t>(info.region.bearing_x);
    inst.offset_y = 0;  // caller handles Y offset via grid_row
    inst.fg_r = (color >> 16) & 0xFF;
    inst.fg_g = (color >> 8) & 0xFF;
    inst.fg_b = color & 0xFF;
    inst.fg_a = 255;
    inst.flags = 1;  // has_glyph -- but we need overlay positioning
    return inst;
}

// Blend two RGB colors by factor t (0.0 = base, 1.0 = target)
static uint32_t blendColor(uint32_t base, uint32_t target, float t) {
    int bR = (base >> 16) & 0xFF, bG = (base >> 8) & 0xFF, bB = base & 0xFF;
    int tR = (target >> 16) & 0xFF, tG = (target >> 8) & 0xFF, tB = target & 0xFF;
    return (static_cast<uint32_t>(bR + (tR - bR) * t) << 16) |
           (static_cast<uint32_t>(bG + (tG - bG) * t) << 8) |
            static_cast<uint32_t>(bB + (tB - bB) * t);
}

void MetalTextRenderer::Impl::buildOverlayPasses(const Screen& screen,
                                                   float cellW, float cellH,
                                                   float ascent,
                                                   float fontSize) {
    // Pass 8: Tab Bar (polished, modern design matching Windows D3D implementation)
    const auto& tb = this->tabBar;
    if (!tb.visible || tb.tabs.empty()) return;

    float tabBarH = cellH * MetalTextRenderer::kTabBarHeightScale;
    int tabCount = static_cast<int>(tb.tabs.size());

    // Layout constants (matching Windows exactly)
    float tabPadX = cellW * 1.0f;
    float tabGap = 4.0f;
    float tabMinW = cellW * 12.0f;
    float tabMaxW = cellW * 24.0f;
    float closeW = cellW * 1.5f;
    float leftMargin = 8.0f;
    float tabTopPad = 6.0f;
    float bottomBorderH = 1.0f;
    float plusBtnW = cellW * 2.0f;

    float availW = viewportWidth - leftMargin - plusBtnW - 8.0f;
    float tabW = (availW - tabGap * (tabCount - 1)) / tabCount;
    tabW = std::max(tabMinW, std::min(tabMaxW, tabW));

    // Subtle tint overlay on top of Pass 1b base background.
    // Dark themes: lighten slightly; light themes: darken slightly.
    {
        uint32_t bgR = (tb.bg_color >> 16) & 0xFF;
        uint32_t bgG = (tb.bg_color >> 8) & 0xFF;
        uint32_t bgB = tb.bg_color & 0xFF;
        int lum = bgR * 299 + bgG * 587 + bgB * 114;
        bool isDark = lum < 128000;

        CellInstance tint = {};
        tint.grid_col = 0;
        tint.grid_row = 0;
        tint.glyph_width = static_cast<uint16_t>(viewportWidth);
        tint.glyph_height = static_cast<uint16_t>(tabBarH);
        if (isDark) {
            // Dark theme: subtle white overlay (lighten)
            uint8_t v = static_cast<uint8_t>(255 * 0.06f);
            tint.bg_r = v; tint.bg_g = v; tint.bg_b = v;
            tint.bg_a = static_cast<uint8_t>(255 * 0.06f);
        } else {
            // Light theme: subtle black overlay (darken)
            tint.bg_r = 0; tint.bg_g = 0; tint.bg_b = 0;
            tint.bg_a = static_cast<uint8_t>(255 * 0.05f);
        }
        tint.flags = 8;
        cellInstances.push_back(tint);
    }

    // Bottom border line (full width, will be covered by active tab)
    {
        uint32_t borderColor = blendColor(tb.bg_color, tb.fg_color, 0.18f);
        cellInstances.push_back(makeOverlayRectRGB(
            0, tabBarH - bottomBorderH, viewportWidth, bottomBorderH, borderColor));
    }

    for (int t = 0; t < tabCount; ++t) {
        const auto& tab = tb.tabs[t];
        float tabX = leftMargin + t * (tabW + tabGap);
        if (tabX >= viewportWidth) break;
        bool isHovered = (t == tb.hovered_tab);

        // Tab dimensions: active tabs are taller (touch bottom), inactive float
        float tabY, tabH;
        if (tab.active) {
            tabY = tabTopPad;
            tabH = tabBarH - tabTopPad;
        } else {
            tabY = tabTopPad + 2.0f;
            tabH = tabBarH - tabTopPad - 2.0f - bottomBorderH;
        }

        // Tab background color
        uint32_t tabBgColor;
        if (tab.active) {
            tabBgColor = tb.active_bg_color;
        } else if (tab.needs_attention) {
            tabBgColor = blendColor(tb.bg_color, tb.accent_color, 0.25f);
        } else if (isHovered) {
            tabBgColor = blendColor(tb.bg_color, tb.fg_color, 0.08f);
        } else {
            tabBgColor = blendColor(tb.bg_color, tb.fg_color, 0.04f);
        }

        // Tab background rect
        // Use flag 4 (bg pass) style but at overlay position
        cellInstances.push_back(makeOverlayRectRGB(tabX, tabY, tabW, tabH, tabBgColor));

        // Active tab: accent indicator at top edge
        if (tab.active) {
            cellInstances.push_back(makeOverlayRectRGB(tabX, tabY, tabW, 2.0f, tb.accent_color));
        }

        // Separator: thin vertical line between non-active/non-hovered tabs
        if (t > 0 && !tab.active && !tb.tabs[t - 1].active
            && t != tb.hovered_tab && t - 1 != tb.hovered_tab) {
            uint32_t sepColor = blendColor(tb.bg_color, tb.fg_color, 0.1f);
            cellInstances.push_back(makeOverlayRectRGB(
                tabX - tabGap * 0.5f, tabTopPad + tabH * 0.2f,
                1.0f, tabH * 0.6f, sepColor));
        }

        // Text color with proper contrast
        uint32_t textColor;
        if (tab.active) {
            textColor = tb.fg_color;
        } else if (isHovered) {
            textColor = blendColor(tb.fg_color, tb.bg_color, 0.1f);
        } else {
            textColor = blendColor(tb.fg_color, tb.bg_color, 0.4f);
        }

        // Tab title text (vertically centered in tab)
        bool showClose = (tab.active || isHovered);
        const std::string& title = tab.title;
        float textX = tabX + tabPadX;
        float textMaxX = tabX + tabW - (showClose ? closeW + 4.0f : tabPadX);
        float textCenterY = tabY + (tabH - cellH) * 0.5f;

        // Icon prefix: use OSC 1 icon_name if set, otherwise look up config map
        char32_t icon = firstCodepoint(tab.icon_name);
        if (icon == 0 && !tab.process_name.empty() && tb.process_icon_map) {
            auto it = tb.process_icon_map->find(tab.process_name);
            if (it != tb.process_icon_map->end()) {
                icon = static_cast<char32_t>(
                    std::stoul(it->second, nullptr, 16));
            }
        }
        if (icon != 0 && fontCollection) {
            uint32_t iconColor = tab.active
                ? tb.accent_color
                : blendColor(tb.accent_color, tb.bg_color, 0.4f);
            CollectionFaceId iconFace = fontCollection->resolveFace(icon);
            if (iconFace != kInvalidCollectionFace) {
                FontFaceId iconRastFace = fontCollection->rasterizerFaceId(iconFace);
                uint32_t iconGlyphIdx = rasterizer->getGlyphIndex(iconRastFace, icon);
                if (iconGlyphIdx != 0) {
                    GlyphKey iconKey{iconRastFace, iconGlyphIdx, {0, 0}};
                    auto iconInfo = glyphCache->getOrRasterize(
                        iconKey, fontSize, *rasterizer, *glyphAtlas);
                    if (iconInfo && iconInfo->region.width > 0) {
                        CellInstance iconInst = {};
                        float glyphX = textX + iconInfo->region.bearing_x;
                        float glyphY = textCenterY + ascent - iconInfo->region.bearing_y;
                        iconInst.grid_col = static_cast<uint16_t>(glyphX);
                        iconInst.grid_row = static_cast<uint16_t>(glyphY);
                        iconInst.glyph_x = static_cast<uint16_t>(iconInfo->region.x);
                        iconInst.glyph_y = static_cast<uint16_t>(iconInfo->region.y);
                        iconInst.glyph_width = static_cast<uint16_t>(iconInfo->region.width);
                        iconInst.glyph_height = static_cast<uint16_t>(iconInfo->region.height);
                        iconInst.fg_r = (iconColor >> 16) & 0xFF;
                        iconInst.fg_g = (iconColor >> 8) & 0xFF;
                        iconInst.fg_b = iconColor & 0xFF;
                        iconInst.fg_a = 255;
                        // Use overlay flag + has_glyph for overlay-positioned glyph
                        iconInst.flags = 8 | 1;
                        cellInstances.push_back(iconInst);
                    }
                }
            }
            textX += cellW * 1.2f;  // space after icon
        }

        if (fontCollection) {
            for (size_t i = 0; i < title.size(); ++i) {
                char32_t cp = static_cast<char32_t>(
                    static_cast<unsigned char>(title[i]));
                if (cp == ' ' || cp == 0) { textX += cellW; continue; }

                CollectionFaceId faceId = fontCollection->resolveFace(cp);
                if (faceId == kInvalidCollectionFace) { textX += cellW; continue; }
                FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);
                uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
                if (glyphIdx == 0) { textX += cellW; continue; }

                GlyphKey key{rastFace, glyphIdx, {0, 0}};
                auto info = glyphCache->getOrRasterize(
                    key, fontSize, *rasterizer, *glyphAtlas);
                if (!info || info->region.width <= 0) { textX += cellW; continue; }
                if (textX + info->region.width > textMaxX) break;

                CellInstance inst = {};
                float glyphX = textX + info->region.bearing_x;
                float glyphY = textCenterY + ascent - info->region.bearing_y;
                inst.grid_col = static_cast<uint16_t>(glyphX);
                inst.grid_row = static_cast<uint16_t>(glyphY);
                inst.glyph_x = static_cast<uint16_t>(info->region.x);
                inst.glyph_y = static_cast<uint16_t>(info->region.y);
                inst.glyph_width = static_cast<uint16_t>(info->region.width);
                inst.glyph_height = static_cast<uint16_t>(info->region.height);
                inst.fg_r = (textColor >> 16) & 0xFF;
                inst.fg_g = (textColor >> 8) & 0xFF;
                inst.fg_b = textColor & 0xFF;
                inst.fg_a = 255;
                inst.flags = 8 | 1;  // overlay + has_glyph
                cellInstances.push_back(inst);
                textX += cellW;
            }
        }

        // Close button: only on active or hovered tab
        if (showClose && fontCollection) {
            float closeCenterX = tabX + tabW - closeW * 0.5f - 4.0f;
            float closeCenterY = tabY + tabH * 0.5f;
            float closeBtnSize = cellH * 0.55f;

            // Close hover highlight (rounded-look square bg)
            if (isHovered && tb.hover_close) {
                uint32_t hoverBg = blendColor(tabBgColor, tb.fg_color, 0.15f);
                cellInstances.push_back(makeOverlayRectRGB(
                    closeCenterX - closeBtnSize * 0.5f,
                    closeCenterY - closeBtnSize * 0.5f,
                    closeBtnSize, closeBtnSize, hoverBg));
            }

            // multiply sign glyph
            char32_t xCp = U'\u00D7';
            CollectionFaceId xFace = fontCollection->resolveFace(xCp);
            if (xFace == kInvalidCollectionFace) {
                xCp = 'x';
                xFace = fontCollection->resolveFace(xCp);
            }
            if (xFace != kInvalidCollectionFace) {
                FontFaceId xRast = fontCollection->rasterizerFaceId(xFace);
                uint32_t xGlyph = rasterizer->getGlyphIndex(xRast, xCp);
                if (xGlyph != 0) {
                    GlyphKey xKey{xRast, xGlyph, {0, 0}};
                    auto xInfo = glyphCache->getOrRasterize(
                        xKey, fontSize, *rasterizer, *glyphAtlas);
                    if (xInfo && xInfo->region.width > 0) {
                        CellInstance xInst = {};
                        float gx = closeCenterX - xInfo->region.width * 0.5f
                                   + xInfo->region.bearing_x;
                        float gy = textCenterY + ascent - xInfo->region.bearing_y;
                        xInst.grid_col = static_cast<uint16_t>(gx);
                        xInst.grid_row = static_cast<uint16_t>(gy);
                        xInst.glyph_x = static_cast<uint16_t>(xInfo->region.x);
                        xInst.glyph_y = static_cast<uint16_t>(xInfo->region.y);
                        xInst.glyph_width = static_cast<uint16_t>(xInfo->region.width);
                        xInst.glyph_height = static_cast<uint16_t>(xInfo->region.height);
                        uint32_t closeFg = (isHovered && tb.hover_close)
                            ? tb.fg_color
                            : blendColor(textColor, tabBgColor, 0.3f);
                        xInst.fg_r = (closeFg >> 16) & 0xFF;
                        xInst.fg_g = (closeFg >> 8) & 0xFF;
                        xInst.fg_b = closeFg & 0xFF;
                        xInst.fg_a = 255;
                        xInst.flags = 8 | 1;
                        cellInstances.push_back(xInst);
                    }
                }
            }
        }

        // Unread dot indicator
        if (tab.has_unread && !tab.active) {
            float dotR = 3.0f;
            float dotX = tabX + tabW - dotR * 3 - 2.0f;
            float dotY = tabY + tabH * 0.5f - dotR;
            cellInstances.push_back(makeOverlayRectRGB(
                dotX, dotY, dotR * 2, dotR * 2, tb.accent_color));
        }
    }

    // "+" new tab button (with hover effect)
    {
        float plusX = leftMargin + tabCount * (tabW + tabGap) + 4.0f;
        float plusY = tabTopPad + 2.0f;
        float plusH = tabBarH - tabTopPad - 2.0f - bottomBorderH;
        if (plusX + plusBtnW < viewportWidth && fontCollection) {
            // Hover background for "+" button
            if (tb.hover_plus) {
                uint32_t hoverBg = blendColor(tb.bg_color, tb.fg_color, 0.1f);
                cellInstances.push_back(makeOverlayRectRGB(
                    plusX, plusY, plusBtnW, plusH, hoverBg));
            }

            char32_t plusCp = '+';
            CollectionFaceId pFace = fontCollection->resolveFace(plusCp);
            if (pFace != kInvalidCollectionFace) {
                FontFaceId pRast = fontCollection->rasterizerFaceId(pFace);
                uint32_t pGlyph = rasterizer->getGlyphIndex(pRast, plusCp);
                if (pGlyph != 0) {
                    GlyphKey pKey{pRast, pGlyph, {0, 0}};
                    auto pInfo = glyphCache->getOrRasterize(
                        pKey, fontSize, *rasterizer, *glyphAtlas);
                    if (pInfo && pInfo->region.width > 0) {
                        CellInstance pInst = {};
                        float gx = plusX + (plusBtnW - pInfo->region.width) * 0.5f
                                   + pInfo->region.bearing_x;
                        float plusTextY = plusY + (plusH - cellH) * 0.5f;
                        float gy = plusTextY + ascent - pInfo->region.bearing_y;
                        pInst.grid_col = static_cast<uint16_t>(gx);
                        pInst.grid_row = static_cast<uint16_t>(gy);
                        pInst.glyph_x = static_cast<uint16_t>(pInfo->region.x);
                        pInst.glyph_y = static_cast<uint16_t>(pInfo->region.y);
                        pInst.glyph_width = static_cast<uint16_t>(pInfo->region.width);
                        pInst.glyph_height = static_cast<uint16_t>(pInfo->region.height);
                        uint32_t plusFg = tb.hover_plus
                            ? blendColor(tb.fg_color, tb.bg_color, 0.1f)
                            : blendColor(tb.fg_color, tb.bg_color, 0.5f);
                        pInst.fg_r = (plusFg >> 16) & 0xFF;
                        pInst.fg_g = (plusFg >> 8) & 0xFF;
                        pInst.fg_b = plusFg & 0xFF;
                        pInst.fg_a = 255;
                        pInst.flags = 8 | 1;
                        cellInstances.push_back(pInst);
                    }
                }
            }
        }
    }
}

} // namespace termcore
