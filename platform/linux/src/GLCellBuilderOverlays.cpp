#include "GLTextRendererImpl.h"

#include <string>

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

void GLTextRenderer::Impl::buildOverlayPasses(const Screen& screen,
                                                float cellW, float cellH,
                                                float ascent,
                                                float fontSize) {
    int rows = screen.rows();

    // Pass 5: Status Bar
    const auto& statusBar = this->statusBar;
    if (statusBar.visible) {
        float statusY = viewportHeight - cellH;
        int statusCols = static_cast<int>(viewportWidth / cellW);

        // Background for entire status bar
        GLCellInstance bgInst = {};
        bgInst.position[0] = 0;
        bgInst.position[1] = statusY;
        bgInst.atlas_size[0] = viewportWidth;
        bgInst.atlas_size[1] = cellH;
        colorFromRGBA(statusBar.bg_color | 0xFF000000, bgInst.bg_color);
        bgInst.flags = 4;  // is_bg_pass
        bgInst.extra_flags = 0;
        cellInstances.push_back(bgInst);

        // Render status bar text characters
        auto renderStatusText = [&](const std::string& text, int startCol) {
            for (size_t i = 0; i < text.size() && startCol + static_cast<int>(i) < statusCols; ++i) {
                char32_t cp = static_cast<char32_t>(static_cast<unsigned char>(text[i]));
                if (cp == ' ' || cp == 0) continue;

                CollectionFaceId faceId = fontCollection->resolveFace(cp);
                if (faceId == kInvalidCollectionFace) continue;

                FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);
                uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
                if (glyphIdx == 0) continue;

                GlyphKey key{rastFace, glyphIdx, {0, 0}};
                float fontSize_ = fontCollection->fontSize();
                auto info = glyphCache->getOrRasterize(key, fontSize_, *rasterizer, *glyphAtlas);
                if (!info || info->region.width <= 0) continue;

                GLCellInstance inst = {};
                int col = startCol + static_cast<int>(i);
                float offsetX = static_cast<float>(info->region.bearing_x);
                float offsetY = ascent - static_cast<float>(info->region.bearing_y);

                inst.position[0] = col * cellW + offsetX;
                inst.position[1] = statusY + offsetY;
                inst.atlas_uv[0] = static_cast<float>(info->region.x);
                inst.atlas_uv[1] = static_cast<float>(info->region.y);
                inst.atlas_size[0] = static_cast<float>(info->region.width);
                inst.atlas_size[1] = static_cast<float>(info->region.height);
                colorFromRGBA(statusBar.fg_color | 0xFF000000, inst.fg_color);
                inst.flags = 1;  // has_glyph
                inst.extra_flags = 0;
                cellInstances.push_back(inst);
            }
        };

        renderStatusText(statusBar.left_text, 1);

        int rightStart = statusCols - static_cast<int>(statusBar.right_text.size()) - 1;
        if (rightStart > 0) {
            renderStatusText(statusBar.right_text, rightStart);
        }

        int centerStart = (statusCols - static_cast<int>(statusBar.center_text.size())) / 2;
        if (centerStart > 0) {
            renderStatusText(statusBar.center_text, centerStart);
        }
    }

    // Pass 6: Scrollbar indicator (when scrolled up)
    if (screen.viewportOffset() > 0) {
        int totalLines = static_cast<int>(screen.scrollbackSize()) + rows;
        int visibleLines = rows;
        float scrollFraction = 1.0f - static_cast<float>(screen.viewportOffset()) /
                               static_cast<float>(totalLines - visibleLines);

        float scrollbarHeight = std::max(20.0f, (static_cast<float>(visibleLines) / totalLines) * viewportHeight);
        float scrollbarY = scrollFraction * (viewportHeight - scrollbarHeight);
        float scrollbarWidth = 6.0f;
        float scrollbarX = viewportWidth - scrollbarWidth;

        // Scrollbar track
        GLCellInstance track = {};
        track.position[0] = scrollbarX;
        track.position[1] = 0;
        track.atlas_size[0] = scrollbarWidth;
        track.atlas_size[1] = viewportHeight;
        track.bg_color[0] = 1.0f; track.bg_color[1] = 1.0f;
        track.bg_color[2] = 1.0f; track.bg_color[3] = 0.05f;
        track.flags = 8;
        track.extra_flags = 0;
        cellInstances.push_back(track);

        // Scrollbar thumb
        GLCellInstance thumb = {};
        thumb.position[0] = scrollbarX;
        thumb.position[1] = scrollbarY;
        thumb.atlas_size[0] = scrollbarWidth;
        thumb.atlas_size[1] = scrollbarHeight;
        thumb.bg_color[0] = 1.0f; thumb.bg_color[1] = 1.0f;
        thumb.bg_color[2] = 1.0f; thumb.bg_color[3] = 0.3f;
        thumb.flags = 8;
        thumb.extra_flags = 0;
        cellInstances.push_back(thumb);
    }

    // Pass 7: Resize Overlay
    if (resizeOverlayVisible && resizeOverlayCols > 0 && resizeOverlayRows > 0) {
        std::string text = std::to_string(resizeOverlayCols) + " x " + std::to_string(resizeOverlayRows);

        float boxW = (text.size() + 2) * cellW;
        float boxH = cellH * 1.5f;
        float boxX = (viewportWidth - boxW) / 2.0f;
        float boxY = (viewportHeight - boxH) / 2.0f;

        GLCellInstance bg = {};
        bg.position[0] = boxX;
        bg.position[1] = boxY;
        bg.atlas_size[0] = boxW;
        bg.atlas_size[1] = boxH;
        bg.bg_color[0] = 0.0f; bg.bg_color[1] = 0.0f;
        bg.bg_color[2] = 0.0f; bg.bg_color[3] = 0.7f;
        bg.flags = 8;
        bg.extra_flags = 0;
        cellInstances.push_back(bg);

        float textStartX = boxX + cellW;
        float textY = boxY + (boxH - cellH) / 2.0f;

        for (size_t i = 0; i < text.size(); ++i) {
            char32_t cp = static_cast<char32_t>(static_cast<unsigned char>(text[i]));
            if (cp == ' ') continue;

            CollectionFaceId faceId = fontCollection->resolveFace(cp);
            if (faceId == kInvalidCollectionFace) continue;
            FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);
            uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
            if (glyphIdx == 0) continue;

            GlyphKey key{rastFace, glyphIdx, {0, 0}};
            float fontSize_ = fontCollection->fontSize();
            auto info = glyphCache->getOrRasterize(key, fontSize_, *rasterizer, *glyphAtlas);
            if (!info || info->region.width <= 0) continue;

            GLCellInstance inst = {};
            float offsetX = static_cast<float>(info->region.bearing_x);
            float offsetY = ascent - static_cast<float>(info->region.bearing_y);
            inst.position[0] = textStartX + i * cellW + offsetX;
            inst.position[1] = textY + offsetY;
            inst.atlas_uv[0] = static_cast<float>(info->region.x);
            inst.atlas_uv[1] = static_cast<float>(info->region.y);
            inst.atlas_size[0] = static_cast<float>(info->region.width);
            inst.atlas_size[1] = static_cast<float>(info->region.height);
            inst.fg_color[0] = 1.0f; inst.fg_color[1] = 1.0f;
            inst.fg_color[2] = 1.0f; inst.fg_color[3] = 1.0f;
            inst.flags = 1;
            inst.extra_flags = 0;
            cellInstances.push_back(inst);
        }
    }

    // Pass 8: Tab Bar
    const auto& tabBar = this->tabBar;
    if (tabBar.visible && !tabBar.tabs.empty()) {
        float tabBarH = cellH * GLTextRenderer::kTabBarHeightScale;
        int tabCount = static_cast<int>(tabBar.tabs.size());

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

        auto blendColor = [](uint32_t base, uint32_t target, float t) -> uint32_t {
            int bR = (base >> 16) & 0xFF, bG = (base >> 8) & 0xFF, bB = base & 0xFF;
            int tR = (target >> 16) & 0xFF, tG = (target >> 8) & 0xFF, tB = target & 0xFF;
            return ((uint32_t)(bR + (tR - bR) * t) << 16) |
                   ((uint32_t)(bG + (tG - bG) * t) << 8) |
                    (uint32_t)(bB + (tB - bB) * t);
        };

        // Subtle tint overlay on top of Pass 1b base background.
        // Dark themes: lighten slightly; light themes: darken slightly.
        // This gives the tab bar a distinct look while keeping the same blur opacity.
        {
            GLCellInstance tint = {};
            tint.position[0] = 0;
            tint.position[1] = 0;
            tint.atlas_size[0] = viewportWidth;
            tint.atlas_size[1] = tabBarH;
            uint32_t bgR = (tabBar.bg_color >> 16) & 0xFF;
            uint32_t bgG = (tabBar.bg_color >> 8) & 0xFF;
            uint32_t bgB = tabBar.bg_color & 0xFF;
            int lum = bgR * 299 + bgG * 587 + bgB * 114;
            bool isDark = lum < 128000;
            if (isDark) {
                // Dark theme: subtle white overlay (lighten)
                tint.bg_color[0] = 1.0f * 0.06f;
                tint.bg_color[1] = 1.0f * 0.06f;
                tint.bg_color[2] = 1.0f * 0.06f;
                tint.bg_color[3] = 0.06f;
            } else {
                // Light theme: subtle black overlay (darken)
                tint.bg_color[0] = 0.0f;
                tint.bg_color[1] = 0.0f;
                tint.bg_color[2] = 0.0f;
                tint.bg_color[3] = 0.05f;
            }
            tint.flags = 8;
            tint.extra_flags = 0;
            cellInstances.push_back(tint);
        }

        // Bottom border line
        {
            uint32_t borderColor = blendColor(tabBar.bg_color, tabBar.fg_color, 0.18f);
            GLCellInstance border = {};
            border.position[0] = 0;
            border.position[1] = tabBarH - bottomBorderH;
            border.atlas_size[0] = viewportWidth;
            border.atlas_size[1] = bottomBorderH;
            colorFromRGBA(borderColor | 0xFF000000, border.bg_color);
            border.flags = 8;
            border.extra_flags = 0;
            cellInstances.push_back(border);
        }

        for (int t = 0; t < tabCount; ++t) {
            const auto& tab = tabBar.tabs[t];
            float tabX = leftMargin + t * (tabW + tabGap);
            if (tabX >= viewportWidth) break;
            bool isHovered = (t == tabBar.hovered_tab);

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
                tabBgColor = tabBar.active_bg_color;  // = terminal bg (seamless with content)
            } else if (tab.needs_attention) {
                tabBgColor = blendColor(tabBar.bg_color, tabBar.accent_color, 0.25f);
            } else if (isHovered) {
                tabBgColor = blendColor(tabBar.bg_color, tabBar.fg_color, 0.08f);
            } else {
                tabBgColor = blendColor(tabBar.bg_color, tabBar.fg_color, 0.04f);
            }

            // Tab background rect (always draw for all tabs)
            {
                GLCellInstance tabBg = {};
                tabBg.position[0] = tabX;
                tabBg.position[1] = tabY;
                tabBg.atlas_size[0] = tabW;
                tabBg.atlas_size[1] = tabH;
                colorFromRGBA(tabBgColor | 0xFF000000, tabBg.bg_color);
                tabBg.flags = 4;
                tabBg.extra_flags = 0;
                cellInstances.push_back(tabBg);
            }

            if (tab.active) {
                GLCellInstance indicator = {};
                indicator.position[0] = tabX;
                indicator.position[1] = tabY;
                indicator.atlas_size[0] = tabW;
                indicator.atlas_size[1] = 2.0f;
                colorFromRGBA(tabBar.accent_color | 0xFF000000, indicator.bg_color);
                indicator.flags = 8;
                indicator.extra_flags = 0;
                cellInstances.push_back(indicator);
            }

            if (t > 0 && !tab.active && !tabBar.tabs[t - 1].active
                && t != tabBar.hovered_tab && t - 1 != tabBar.hovered_tab) {
                GLCellInstance sep = {};
                sep.position[0] = tabX - tabGap * 0.5f;
                sep.position[1] = tabTopPad + tabH * 0.2f;
                sep.atlas_size[0] = 1.0f;
                sep.atlas_size[1] = tabH * 0.6f;
                uint32_t sepColor = blendColor(tabBar.bg_color, tabBar.fg_color, 0.1f);
                colorFromRGBA(sepColor | 0xFF000000, sep.bg_color);
                sep.flags = 8;
                sep.extra_flags = 0;
                cellInstances.push_back(sep);
            }

            uint32_t textColor;
            if (tab.active) {
                textColor = tabBar.fg_color;
            } else if (isHovered) {
                textColor = blendColor(tabBar.fg_color, tabBar.bg_color, 0.1f);
            } else {
                textColor = blendColor(tabBar.fg_color, tabBar.bg_color, 0.4f);
            }

            bool showClose = (tab.active || isHovered);
            const std::string& title = tab.title;
            float textX = tabX + tabPadX;
            float textMaxX = tabX + tabW - (showClose ? closeW + 4.0f : tabPadX);
            float textCenterY = tabY + (tabH - cellH) * 0.5f;

            // Icon prefix: use OSC 1 icon_name if set, otherwise look up config map
            char32_t icon = firstCodepoint(tab.icon_name);
            if (icon == 0 && !tab.process_name.empty() && tabBar.process_icon_map) {
                auto it = tabBar.process_icon_map->find(tab.process_name);
                if (it != tabBar.process_icon_map->end()) {
                    icon = static_cast<char32_t>(
                        std::stoul(it->second, nullptr, 16));
                }
            }
            if (icon != 0) {
                uint32_t iconColor = tab.active
                    ? tabBar.accent_color
                    : blendColor(tabBar.accent_color, tabBar.bg_color, 0.4f);
                CollectionFaceId iconFace = fontCollection->resolveFace(icon);
                if (iconFace != kInvalidCollectionFace) {
                    FontFaceId iconRastFace = fontCollection->rasterizerFaceId(iconFace);
                    uint32_t iconGlyphIdx = rasterizer->getGlyphIndex(iconRastFace, icon);
                    if (iconGlyphIdx != 0) {
                        GlyphKey iconKey{iconRastFace, iconGlyphIdx, {0, 0}};
                        auto iconInfo = glyphCache->getOrRasterize(
                            iconKey, fontSize, *rasterizer, *glyphAtlas);
                        if (iconInfo && iconInfo->region.width > 0) {
                            GLCellInstance iconInst = {};
                            iconInst.position[0] = textX + iconInfo->region.bearing_x;
                            iconInst.position[1] = textCenterY + ascent - iconInfo->region.bearing_y;
                            iconInst.atlas_uv[0] = (float)iconInfo->region.x;
                            iconInst.atlas_uv[1] = (float)iconInfo->region.y;
                            iconInst.atlas_size[0] = (float)iconInfo->region.width;
                            iconInst.atlas_size[1] = (float)iconInfo->region.height;
                            colorFromRGBA(iconColor | 0xFF000000, iconInst.fg_color);
                            iconInst.flags = 1;
                            iconInst.extra_flags = 0;
                            cellInstances.push_back(iconInst);
                        }
                    }
                }
                textX += cellW * 1.2f;
            }

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

                GLCellInstance inst = {};
                inst.position[0] = textX + info->region.bearing_x;
                inst.position[1] = textCenterY + ascent - info->region.bearing_y;
                inst.atlas_uv[0] = (float)info->region.x;
                inst.atlas_uv[1] = (float)info->region.y;
                inst.atlas_size[0] = (float)info->region.width;
                inst.atlas_size[1] = (float)info->region.height;
                colorFromRGBA(textColor | 0xFF000000, inst.fg_color);
                inst.flags = 1;
                inst.extra_flags = 0;
                cellInstances.push_back(inst);
                textX += cellW;
            }

            // Close button
            if (showClose) {
                float closeCenterX = tabX + tabW - closeW * 0.5f - 4.0f;
                float closeCenterY = tabY + tabH * 0.5f;
                float closeBtnSize = cellH * 0.55f;

                if (isHovered && tabBar.hover_close) {
                    GLCellInstance cBg = {};
                    cBg.position[0] = closeCenterX - closeBtnSize * 0.5f;
                    cBg.position[1] = closeCenterY - closeBtnSize * 0.5f;
                    cBg.atlas_size[0] = closeBtnSize;
                    cBg.atlas_size[1] = closeBtnSize;
                    uint32_t hoverBg = blendColor(tabBgColor, tabBar.fg_color, 0.15f);
                    colorFromRGBA(hoverBg | 0xFF000000, cBg.bg_color);
                    cBg.flags = 8;
                    cBg.extra_flags = 0;
                    cellInstances.push_back(cBg);
                }

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
                            GLCellInstance xInst = {};
                            xInst.position[0] = closeCenterX - xInfo->region.width * 0.5f
                                                + xInfo->region.bearing_x;
                            xInst.position[1] = textCenterY + ascent - xInfo->region.bearing_y;
                            xInst.atlas_uv[0] = (float)xInfo->region.x;
                            xInst.atlas_uv[1] = (float)xInfo->region.y;
                            xInst.atlas_size[0] = (float)xInfo->region.width;
                            xInst.atlas_size[1] = (float)xInfo->region.height;
                            uint32_t closeFg = (isHovered && tabBar.hover_close)
                                ? tabBar.fg_color
                                : blendColor(textColor, tabBgColor, 0.3f);
                            colorFromRGBA(closeFg | 0xFF000000, xInst.fg_color);
                            xInst.flags = 1;
                            xInst.extra_flags = 0;
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
                GLCellInstance dot = {};
                dot.position[0] = dotX;
                dot.position[1] = dotY;
                dot.atlas_size[0] = dotR * 2;
                dot.atlas_size[1] = dotR * 2;
                colorFromRGBA(tabBar.accent_color | 0xFF000000, dot.bg_color);
                dot.flags = 8;
                dot.extra_flags = 0;
                cellInstances.push_back(dot);
            }
        }

        // "+" new tab button
        {
            float plusX = leftMargin + tabCount * (tabW + tabGap) + 4.0f;
            float plusY = tabTopPad + 2.0f;
            float plusH = tabBarH - tabTopPad - 2.0f - bottomBorderH;
            if (plusX + plusBtnW < viewportWidth) {
                if (tabBar.hover_plus) {
                    GLCellInstance plusBg = {};
                    plusBg.position[0] = plusX;
                    plusBg.position[1] = plusY;
                    plusBg.atlas_size[0] = plusBtnW;
                    plusBg.atlas_size[1] = plusH;
                    uint32_t hoverBg = blendColor(tabBar.bg_color, tabBar.fg_color, 0.1f);
                    colorFromRGBA(hoverBg | 0xFF000000, plusBg.bg_color);
                    plusBg.flags = 8;
                    plusBg.extra_flags = 0;
                    cellInstances.push_back(plusBg);
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
                            GLCellInstance pInst = {};
                            pInst.position[0] = plusX + (plusBtnW - pInfo->region.width) * 0.5f
                                + pInfo->region.bearing_x;
                            float plusTextY = plusY + (plusH - cellH) * 0.5f;
                            pInst.position[1] = plusTextY + ascent - pInfo->region.bearing_y;
                            pInst.atlas_uv[0] = (float)pInfo->region.x;
                            pInst.atlas_uv[1] = (float)pInfo->region.y;
                            pInst.atlas_size[0] = (float)pInfo->region.width;
                            pInst.atlas_size[1] = (float)pInfo->region.height;
                            uint32_t plusFg = tabBar.hover_plus
                                ? blendColor(tabBar.fg_color, tabBar.bg_color, 0.1f)
                                : blendColor(tabBar.fg_color, tabBar.bg_color, 0.5f);
                            colorFromRGBA(plusFg | 0xFF000000, pInst.fg_color);
                            pInst.flags = 1;
                            pInst.extra_flags = 0;
                            cellInstances.push_back(pInst);
                        }
                    }
                }
            }
        }
    }

    // Pane progress bars and status pills
    buildPaneStatusOverlays(cellW, cellH, ascent, fontSize);

    // Command palette overlay
    buildCommandPaletteOverlay(cellW, cellH, ascent, fontSize);

    // Pass 9: Pane Borders
    const auto& paneBorders = this->paneBorders;
    if (paneBorders.visible && !paneBorders.segments.empty()) {
        for (const auto& seg : paneBorders.segments) {
            GLCellInstance inst = {};
            inst.position[0] = seg.x;
            inst.position[1] = seg.y;
            inst.atlas_size[0] = seg.width;
            inst.atlas_size[1] = seg.height;

            uint32_t borderColor = seg.active
                ? paneBorders.active_color
                : paneBorders.inactive_color;
            colorFromRGBA(borderColor | 0xFF000000, inst.bg_color);
            inst.flags = 8;
            inst.extra_flags = 0;
            cellInstances.push_back(inst);

            // Notification glow border
            if (seg.needs_attention && seg.ring_intensity > 0.0f) {
                float glowThickness = 2.0f;
                float glowColor[4];
                colorFromRGBA(seg.ring_color | 0xFF000000, glowColor);
                glowColor[3] = seg.ring_intensity;

                if (seg.width >= seg.height) {
                    // Horizontal -- glow above
                    GLCellInstance gTop = {};
                    gTop.position[0] = seg.x;
                    gTop.position[1] = seg.y - glowThickness;
                    gTop.atlas_size[0] = seg.width;
                    gTop.atlas_size[1] = glowThickness;
                    gTop.bg_color[0] = glowColor[0]; gTop.bg_color[1] = glowColor[1];
                    gTop.bg_color[2] = glowColor[2]; gTop.bg_color[3] = glowColor[3];
                    gTop.flags = 8;
                    gTop.extra_flags = 0;
                    cellInstances.push_back(gTop);

                    GLCellInstance gBot = {};
                    gBot.position[0] = seg.x;
                    gBot.position[1] = seg.y + seg.height;
                    gBot.atlas_size[0] = seg.width;
                    gBot.atlas_size[1] = glowThickness;
                    gBot.bg_color[0] = glowColor[0]; gBot.bg_color[1] = glowColor[1];
                    gBot.bg_color[2] = glowColor[2]; gBot.bg_color[3] = glowColor[3];
                    gBot.flags = 8;
                    gBot.extra_flags = 0;
                    cellInstances.push_back(gBot);
                } else {
                    // Vertical -- glow left
                    GLCellInstance gLeft = {};
                    gLeft.position[0] = seg.x - glowThickness;
                    gLeft.position[1] = seg.y;
                    gLeft.atlas_size[0] = glowThickness;
                    gLeft.atlas_size[1] = seg.height;
                    gLeft.bg_color[0] = glowColor[0]; gLeft.bg_color[1] = glowColor[1];
                    gLeft.bg_color[2] = glowColor[2]; gLeft.bg_color[3] = glowColor[3];
                    gLeft.flags = 8;
                    gLeft.extra_flags = 0;
                    cellInstances.push_back(gLeft);

                    GLCellInstance gRight = {};
                    gRight.position[0] = seg.x + seg.width;
                    gRight.position[1] = seg.y;
                    gRight.atlas_size[0] = glowThickness;
                    gRight.atlas_size[1] = seg.height;
                    gRight.bg_color[0] = glowColor[0]; gRight.bg_color[1] = glowColor[1];
                    gRight.bg_color[2] = glowColor[2]; gRight.bg_color[3] = glowColor[3];
                    gRight.flags = 8;
                    gRight.extra_flags = 0;
                    cellInstances.push_back(gRight);
                }
            }

            // Unread dot indicator
            if (seg.has_unread && !seg.needs_attention) {
                float dotSize = 4.0f;
                GLCellInstance dot = {};
                dot.position[0] = seg.x + seg.width - dotSize;
                dot.position[1] = seg.y;
                dot.atlas_size[0] = dotSize;
                dot.atlas_size[1] = dotSize;
                dot.bg_color[0] = 0.0f; dot.bg_color[1] = 0.478f;
                dot.bg_color[2] = 0.8f; dot.bg_color[3] = 1.0f;
                dot.flags = 8;
                dot.extra_flags = 0;
                cellInstances.push_back(dot);
            }
        }
    }
}

} // namespace termcore
