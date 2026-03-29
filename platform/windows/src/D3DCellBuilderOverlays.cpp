#if defined(_WIN32)

#include "D3DTextRendererImpl.h"
#include "ScreenSnapshot.h"

#include <chrono>
#include <cmath>
#include <string>

namespace termcore {

// Decode first UTF-8 codepoint from a string
static char32_t firstCodepoint(const std::string& s) {
    size_t pos = 0;
    return nextCodepoint(s, pos);
}

template<typename ScreenT>
void D3DTextRenderer::Impl::buildOverlayPasses(const ScreenT& screen,
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
        D3DCellInstance bgInst = {};
        bgInst.position[0] = 0;
        bgInst.position[1] = statusY;
        bgInst.atlas_size[0] = viewportWidth;  // full width
        bgInst.atlas_size[1] = cellH;
        colorFromRGBA(statusBar.bg_color | 0xFF000000, bgInst.bg_color);
        bgInst.flags = 4;  // is_bg_pass
        cellInstances.push_back(bgInst);

        // Render status bar text characters
        auto renderStatusText = [&](const std::string& text, int startCol) {
            int col = 0;
            for (size_t i = 0; i < text.size() && startCol + col < statusCols; ++col) {
                char32_t cp = nextCodepoint(text, i);
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

                D3DCellInstance inst = {};
                int absCol = startCol + col;
                float offsetX = static_cast<float>(info->region.bearing_x);
                float offsetY = ascent - static_cast<float>(info->region.bearing_y);

                inst.position[0] = absCol * cellW + offsetX;
                inst.position[1] = statusY + offsetY;
                inst.atlas_uv[0] = static_cast<float>(info->region.x);
                inst.atlas_uv[1] = static_cast<float>(info->region.y);
                inst.atlas_size[0] = static_cast<float>(info->region.width);
                inst.atlas_size[1] = static_cast<float>(info->region.height);
                colorFromRGBA(statusBar.fg_color | 0xFF000000, inst.fg_color);
                inst.flags = 1;  // has_glyph
                cellInstances.push_back(inst);
            }
        };

        // Left text at column 1
        renderStatusText(statusBar.left_text, 1);

        // Right text right-aligned
        int rightStart = statusCols - static_cast<int>(statusBar.right_text.size()) - 1;
        if (rightStart > 0) {
            renderStatusText(statusBar.right_text, rightStart);
        }

        // Center text centered
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

        float scrollbarHeight = (std::max)(20.0f, (static_cast<float>(visibleLines) / totalLines) * viewportHeight);
        float scrollbarY = scrollFraction * (viewportHeight - scrollbarHeight);
        float scrollbarWidth = 6.0f;  // 6px wide
        float scrollbarX = viewportWidth - scrollbarWidth;

        // Scrollbar track (semi-transparent)
        D3DCellInstance track = {};
        track.position[0] = scrollbarX;
        track.position[1] = 0;
        track.atlas_size[0] = scrollbarWidth;
        track.atlas_size[1] = viewportHeight;
        track.bg_color[0] = 1.0f; track.bg_color[1] = 1.0f;
        track.bg_color[2] = 1.0f; track.bg_color[3] = 0.05f;
        track.flags = 8;  // solid color (is_cursor)
        cellInstances.push_back(track);

        // Scrollbar thumb
        D3DCellInstance thumb = {};
        thumb.position[0] = scrollbarX;
        thumb.position[1] = scrollbarY;
        thumb.atlas_size[0] = scrollbarWidth;
        thumb.atlas_size[1] = scrollbarHeight;
        thumb.bg_color[0] = 1.0f; thumb.bg_color[1] = 1.0f;
        thumb.bg_color[2] = 1.0f; thumb.bg_color[3] = 0.3f;
        thumb.flags = 8;  // solid color
        cellInstances.push_back(thumb);
    }

    // Pass 7: Resize Overlay
    if (resizeOverlayVisible && resizeOverlayCols > 0 && resizeOverlayRows > 0) {
        std::string text = std::to_string(resizeOverlayCols) + " x " + std::to_string(resizeOverlayRows);

        float boxW = (text.size() + 2) * cellW;
        float boxH = cellH * 1.5f;
        float boxX = (viewportWidth - boxW) / 2.0f;
        float boxY = (viewportHeight - boxH) / 2.0f;

        // Background box
        D3DCellInstance bg = {};
        bg.position[0] = boxX;
        bg.position[1] = boxY;
        bg.atlas_size[0] = boxW;
        bg.atlas_size[1] = boxH;
        bg.bg_color[0] = 0.0f; bg.bg_color[1] = 0.0f;
        bg.bg_color[2] = 0.0f; bg.bg_color[3] = 0.7f;
        bg.flags = 8;  // solid color
        cellInstances.push_back(bg);

        // Text glyphs
        float textStartX = boxX + cellW;
        float textY = boxY + (boxH - cellH) / 2.0f;

        for (size_t i = 0, charIdx = 0; i < text.size(); ++charIdx) {
            char32_t cp = nextCodepoint(text, i);
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

            D3DCellInstance inst = {};
            float offsetX = static_cast<float>(info->region.bearing_x);
            float offsetY = ascent - static_cast<float>(info->region.bearing_y);
            inst.position[0] = textStartX + charIdx * cellW + offsetX;
            inst.position[1] = textY + offsetY;
            inst.atlas_uv[0] = static_cast<float>(info->region.x);
            inst.atlas_uv[1] = static_cast<float>(info->region.y);
            inst.atlas_size[0] = static_cast<float>(info->region.width);
            inst.atlas_size[1] = static_cast<float>(info->region.height);
            inst.fg_color[0] = 1.0f; inst.fg_color[1] = 1.0f;
            inst.fg_color[2] = 1.0f; inst.fg_color[3] = 1.0f;
            inst.flags = 1;  // has_glyph
            cellInstances.push_back(inst);
        }
    }

    // Pass 8: Tab Bar (Windows Terminal-style with rounded top corners)
    const auto& tabBar = this->tabBar;
    if (tabBar.visible && !tabBar.tabs.empty()) {
        float tabBarH = cellH * tabBar.height_scale;
        int tabCount = static_cast<int>(tabBar.tabs.size());

        // Layout constants
        float tabPadX = cellW * 1.0f;         // horizontal padding inside tab
        float tabGap = 4.0f;                   // gap between tabs
        float tabMinW = cellW * 12.0f;
        float tabMaxW = cellW * 24.0f;
        float closeW = cellW * 1.5f;           // close button hit area
        float leftMargin = 8.0f;
        float tabTopPad = 6.0f;                // tabs float below bar top
        float bottomBorderH = 1.0f;            // thin bottom separator
        float plusBtnW = cellW * 2.0f;          // "+" button width
        float activeCornerR = 6.0f;             // active tab corner radius
        float inactiveCornerR = 4.0f;           // inactive tab corner radius
        float accentBarH = 2.0f;               // accent bar at top of active tab
        float closeBtnCornerR = 3.0f;          // close button hover bg radius

        float availW = viewportWidth - leftMargin - plusBtnW - 8.0f;
        float tabW = (availW - tabGap * (tabCount - 1)) / tabCount;
        tabW = (std::max)(tabMinW, (std::min)(tabMaxW, tabW));

        // Helper: encode corner radius into extra_flags upper 16 bits
        auto encodeRadius = [](float radius) -> uint32_t {
            uint32_t encoded = static_cast<uint32_t>(radius * 16.0f);
            return encoded << 16u;
        };

        // Helper to blend two colors
        auto blendColor = [](uint32_t base, uint32_t target, float t) -> uint32_t {
            int bR = (base >> 16) & 0xFF, bG = (base >> 8) & 0xFF, bB = base & 0xFF;
            int tR = (target >> 16) & 0xFF, tG = (target >> 8) & 0xFF, tB = target & 0xFF;
            return ((uint32_t)(bR + (tR - bR) * t) << 16) |
                   ((uint32_t)(bG + (tG - bG) * t) << 8) |
                    (uint32_t)(bB + (tB - bB) * t);
        };

        // Helper to set premultiplied bg color from RGBA
        auto setPremultBg = [](D3DCellInstance& inst, uint32_t rgb, float alpha) {
            float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
            float g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
            float b = static_cast<float>(rgb & 0xFF) / 255.0f;
            inst.bg_color[0] = r * alpha;
            inst.bg_color[1] = g * alpha;
            inst.bg_color[2] = b * alpha;
            inst.bg_color[3] = alpha;
        };

        // Tab bar tint overlay — visually distinct from terminal content area.
        {
            D3DCellInstance tint = {};
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
                tint.bg_color[0] = 0.0f;
                tint.bg_color[1] = 0.0f;
                tint.bg_color[2] = 0.0f;
                tint.bg_color[3] = 0.25f;
            } else {
                tint.bg_color[0] = 0.0f;
                tint.bg_color[1] = 0.0f;
                tint.bg_color[2] = 0.0f;
                tint.bg_color[3] = 0.12f;
            }
            tint.flags = 8;
            cellInstances.push_back(tint);
        }

        // Find active tab index for bottom border interruption
        int activeTabIdx = -1;
        for (int t = 0; t < tabCount; ++t) {
            if (tabBar.tabs[t].active) { activeTabIdx = t; break; }
        }

        // Bottom border line — full width, interrupted where active tab sits
        {
            uint32_t borderColor = blendColor(tabBar.bg_color, tabBar.fg_color, 0.25f);
            float activeTabX = leftMargin + activeTabIdx * (tabW + tabGap);

            if (activeTabIdx >= 0) {
                // Left segment (before active tab)
                if (activeTabX > 0) {
                    D3DCellInstance borderL = {};
                    borderL.position[0] = 0;
                    borderL.position[1] = tabBarH - bottomBorderH;
                    borderL.atlas_size[0] = activeTabX;
                    borderL.atlas_size[1] = bottomBorderH;
                    colorFromRGBA(borderColor | 0xFF000000, borderL.bg_color);
                    borderL.flags = 8;
                    cellInstances.push_back(borderL);
                }
                // Right segment (after active tab)
                float activeTabEnd = activeTabX + tabW;
                if (activeTabEnd < viewportWidth) {
                    D3DCellInstance borderR = {};
                    borderR.position[0] = activeTabEnd;
                    borderR.position[1] = tabBarH - bottomBorderH;
                    borderR.atlas_size[0] = viewportWidth - activeTabEnd;
                    borderR.atlas_size[1] = bottomBorderH;
                    colorFromRGBA(borderColor | 0xFF000000, borderR.bg_color);
                    borderR.flags = 8;
                    cellInstances.push_back(borderR);
                }
            } else {
                // No active tab — full border
                D3DCellInstance border = {};
                border.position[0] = 0;
                border.position[1] = tabBarH - bottomBorderH;
                border.atlas_size[0] = viewportWidth;
                border.atlas_size[1] = bottomBorderH;
                colorFromRGBA(borderColor | 0xFF000000, border.bg_color);
                border.flags = 8;
                cellInstances.push_back(border);
            }
        }

        for (int t = 0; t < tabCount; ++t) {
            const auto& tab = tabBar.tabs[t];
            float tabX = leftMargin + t * (tabW + tabGap);
            if (tabX >= viewportWidth) break;
            bool isHovered = (t == tabBar.hovered_tab);

            // Tab dimensions: active tabs extend to bottom (cover border), inactive float above
            float tabY, tabH;
            float cornerR;
            if (tab.active) {
                tabY = tabTopPad;
                tabH = tabBarH - tabTopPad;  // extends to bottom, seamless with content
                cornerR = activeCornerR;
            } else {
                tabY = tabTopPad + 2.0f;
                tabH = tabBarH - tabTopPad - 2.0f - bottomBorderH;
                cornerR = inactiveCornerR;
            }

            // Tab background color
            uint32_t tabBgColor;
            if (tab.active) {
                tabBgColor = tabBar.active_bg_color;
            } else if (tab.needs_attention) {
                tabBgColor = blendColor(tabBar.bg_color, tabBar.accent_color, 0.3f);
            } else if (isHovered) {
                tabBgColor = blendColor(tabBar.bg_color, tabBar.fg_color, 0.15f);
            } else {
                tabBgColor = blendColor(tabBar.bg_color, tabBar.fg_color, 0.07f);
            }

            // Tab background: rounded top corners via SDF shader
            {
                D3DCellInstance tabBg = {};
                tabBg.position[0] = tabX;
                tabBg.position[1] = tabY;
                tabBg.atlas_size[0] = tabW;
                tabBg.atlas_size[1] = tabH;
                colorFromRGBA(tabBgColor | 0xFF000000, tabBg.bg_color);
                tabBg.flags = 32;  // is_rounded_rect_top
                tabBg.extra_flags = encodeRadius(cornerR);
                cellInstances.push_back(tabBg);
            }

            // Active tab: thin accent color bar at very top of tab
            if (tab.active) {
                D3DCellInstance indicator = {};
                indicator.position[0] = tabX;
                indicator.position[1] = tabY;
                indicator.atlas_size[0] = tabW;
                indicator.atlas_size[1] = accentBarH;
                colorFromRGBA(tabBar.accent_color | 0xFF000000, indicator.bg_color);
                indicator.flags = 32;
                indicator.extra_flags = encodeRadius(cornerR);
                cellInstances.push_back(indicator);
            }

            // Separator: thin vertical line between non-active/non-hovered tabs
            if (t > 0 && !tab.active && !tabBar.tabs[t - 1].active
                && t != tabBar.hovered_tab && t - 1 != tabBar.hovered_tab) {
                D3DCellInstance sep = {};
                sep.position[0] = tabX - tabGap * 0.5f;
                sep.position[1] = tabTopPad + tabH * 0.2f;
                sep.atlas_size[0] = 1.0f;
                sep.atlas_size[1] = tabH * 0.6f;
                uint32_t sepColor = blendColor(tabBar.bg_color, tabBar.fg_color, 0.1f);
                colorFromRGBA(sepColor | 0xFF000000, sep.bg_color);
                sep.flags = 8;
                cellInstances.push_back(sep);
            }

            // Text color: active = full, hovered = slight dim, inactive = more dim
            uint32_t textColor;
            if (tab.active) {
                textColor = tabBar.fg_color;
            } else if (isHovered) {
                textColor = blendColor(tabBar.fg_color, tabBar.bg_color, 0.1f);
            } else {
                textColor = blendColor(tabBar.fg_color, tabBar.bg_color, 0.35f);
            }

            // Tab title text (vertically centered in tab)
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
                            D3DCellInstance iconInst = {};
                            iconInst.position[0] = textX + iconInfo->region.bearing_x;
                            iconInst.position[1] = textCenterY + ascent - iconInfo->region.bearing_y;
                            iconInst.atlas_uv[0] = (float)iconInfo->region.x;
                            iconInst.atlas_uv[1] = (float)iconInfo->region.y;
                            iconInst.atlas_size[0] = (float)iconInfo->region.width;
                            iconInst.atlas_size[1] = (float)iconInfo->region.height;
                            colorFromRGBA(iconColor | 0xFF000000, iconInst.fg_color);
                            iconInst.flags = 1;
                            cellInstances.push_back(iconInst);
                        }
                    }
                }
                textX += cellW * 1.2f;  // space after icon
            }

            for (size_t i = 0; i < title.size(); ) {
                char32_t cp = nextCodepoint(title, i);
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

                D3DCellInstance inst = {};
                inst.position[0] = textX + info->region.bearing_x;
                inst.position[1] = textCenterY + ascent - info->region.bearing_y;
                inst.atlas_uv[0] = (float)info->region.x;
                inst.atlas_uv[1] = (float)info->region.y;
                inst.atlas_size[0] = (float)info->region.width;
                inst.atlas_size[1] = (float)info->region.height;
                colorFromRGBA(textColor | 0xFF000000, inst.fg_color);
                inst.flags = 1;
                cellInstances.push_back(inst);
                textX += cellW;
            }

            // Close button: only on active or hovered tab
            if (showClose) {
                float closeCenterX = tabX + tabW - closeW * 0.5f - 4.0f;
                float closeCenterY = tabY + tabH * 0.5f;
                float closeBtnSize = cellH * 0.55f;

                // Close hover highlight (rounded bg using SDF)
                if (isHovered && tabBar.hover_close) {
                    D3DCellInstance cBg = {};
                    cBg.position[0] = closeCenterX - closeBtnSize * 0.5f;
                    cBg.position[1] = closeCenterY - closeBtnSize * 0.5f;
                    cBg.atlas_size[0] = closeBtnSize;
                    cBg.atlas_size[1] = closeBtnSize;
                    uint32_t hoverBg = blendColor(tabBgColor, tabBar.fg_color, 0.15f);
                    colorFromRGBA(hoverBg | 0xFF000000, cBg.bg_color);
                    cBg.flags = 8;
                    cellInstances.push_back(cBg);
                }

                // "x" glyph
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
                            D3DCellInstance xInst = {};
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
                            cellInstances.push_back(xInst);
                        }
                    }
                }
            }

            // Agent state dot indicator (8x8 px near tab icon area)
            {
                uint32_t stateColor = 0;
                switch (tab.agent_state) {
                    case 3: /* Running */  stateColor = 0xEAB308; break;
                    case 4: /* Thinking */ stateColor = 0xEAB308; break;
                    case 5: /* ToolUse */  stateColor = 0xF97316; break;
                    case 6: /* Waiting */  stateColor = 0x3B82F6; break;
                    case 7: /* Error */    stateColor = 0xEF4444; break;
                    case 2: /* Idle */     stateColor = 0x22C55E; break;
                    default: break;
                }
                if (stateColor != 0) {
                    float dotSize = 8.0f;
                    float dotX = tabX + tabPadX - dotSize - 2.0f;
                    float dotY = tabY + (tabH - dotSize) * 0.5f;
                    // Clamp to tab left boundary
                    if (dotX < tabX + 4.0f) dotX = tabX + 4.0f;
                    D3DCellInstance stateDot = {};
                    stateDot.position[0] = dotX;
                    stateDot.position[1] = dotY;
                    stateDot.atlas_size[0] = dotSize;
                    stateDot.atlas_size[1] = dotSize;
                    colorFromRGBA(stateColor | 0xFF000000, stateDot.bg_color);
                    stateDot.flags = 8;
                    cellInstances.push_back(stateDot);
                }
            }

            // Unread dot indicator
            if (tab.has_unread && !tab.active) {
                float dotR = 3.0f;
                float dotX = tabX + tabW - dotR * 3 - 2.0f;
                float dotY = tabY + tabH * 0.5f - dotR;
                D3DCellInstance dot = {};
                dot.position[0] = dotX;
                dot.position[1] = dotY;
                dot.atlas_size[0] = dotR * 2;
                dot.atlas_size[1] = dotR * 2;
                colorFromRGBA(tabBar.accent_color | 0xFF000000, dot.bg_color);
                dot.flags = 8;
                cellInstances.push_back(dot);
            }

            // Progress bar at tab bottom (2px height)
            if (tab.progress_value >= 0.0f) {
                float barW = tabW * tab.progress_value;
                if (barW > 0.0f) {
                    D3DCellInstance bar = {};
                    bar.position[0] = tabX;
                    bar.position[1] = tabY + tabH - 2.0f;
                    bar.atlas_size[0] = barW;
                    bar.atlas_size[1] = 2.0f;
                    colorFromRGBA(tabBar.accent_color | 0xFF000000, bar.bg_color);
                    bar.flags = 8;
                    cellInstances.push_back(bar);
                }
            }
        }

        // "+" new tab button (with hover effect, rounded top corners)
        {
            float plusX = leftMargin + tabCount * (tabW + tabGap) + 4.0f;
            float plusY = tabTopPad + 2.0f;
            float plusH = tabBarH - tabTopPad - 2.0f - bottomBorderH;
            if (plusX + plusBtnW < viewportWidth) {
                // Hover background for "+" button (rounded top)
                if (tabBar.hover_plus) {
                    D3DCellInstance plusBg = {};
                    plusBg.position[0] = plusX;
                    plusBg.position[1] = plusY;
                    plusBg.atlas_size[0] = plusBtnW;
                    plusBg.atlas_size[1] = plusH;
                    uint32_t hoverBg = blendColor(tabBar.bg_color, tabBar.fg_color, 0.1f);
                    colorFromRGBA(hoverBg | 0xFF000000, plusBg.bg_color);
                    plusBg.flags = 32;  // rounded top corners
                    plusBg.extra_flags = encodeRadius(inactiveCornerR);
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
                            D3DCellInstance pInst = {};
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
                            cellInstances.push_back(pInst);
                        }
                    }
                }
            }
        }
    }

    // Pane progress bars and status pills (delegated to D3DCellBuilderPaneStatus.cpp)
    buildPaneStatusOverlays(cellW, cellH, ascent, fontSize);

    // Command palette overlay (delegated to D3DCellBuilderCommandPalette.cpp)
    buildCommandPaletteOverlay(cellW, cellH, ascent, fontSize);

    // Profile dropdown overlay (delegated to D3DCellBuilderProfileDropdown.cpp)
    buildProfileDropdownOverlay(cellW, cellH, ascent, fontSize);

    // Pass 9: Pane Borders (with notification glow and unread dots)
    const auto& paneBorders = this->paneBorders;
    if (paneBorders.visible && !paneBorders.segments.empty()) {
        for (const auto& seg : paneBorders.segments) {
            D3DCellInstance inst = {};
            inst.position[0] = seg.x;
            inst.position[1] = seg.y;
            inst.atlas_size[0] = seg.width;
            inst.atlas_size[1] = seg.height;

            uint32_t borderColor = seg.active
                ? paneBorders.active_color
                : paneBorders.inactive_color;
            colorFromRGBA(borderColor | 0xFF000000, inst.bg_color);
            inst.flags = 8;  // is_cursor (solid color rect)
            cellInstances.push_back(inst);

            // Notification ring glow: pulsing overlay on border segments.
            // Triggers from notification callbacks or agent state changes.
            if (seg.ring_intensity > 0.0f) {
                // Compute pulsing alpha using sine wave
                auto now = std::chrono::steady_clock::now();
                float time_sec = std::chrono::duration<float>(now.time_since_epoch()).count();
                float pulse = 0.5f + 0.5f * sinf(time_sec * 4.0f);
                float alpha = seg.ring_intensity * pulse;
                float glowThickness = 2.0f + 4.0f * seg.ring_intensity;

                float glowColor[4];
                colorFromRGBA(seg.ring_color | 0xFF000000, glowColor);
                // Apply premultiplied alpha
                glowColor[0] *= alpha;
                glowColor[1] *= alpha;
                glowColor[2] *= alpha;
                glowColor[3] = alpha;

                // Outer glow on each side of the border segment.
                if (seg.width >= seg.height) {
                    // Horizontal border -- glow above
                    D3DCellInstance gTop = {};
                    gTop.position[0] = seg.x;
                    gTop.position[1] = seg.y - glowThickness;
                    gTop.atlas_size[0] = seg.width;
                    gTop.atlas_size[1] = glowThickness;
                    gTop.bg_color[0] = glowColor[0]; gTop.bg_color[1] = glowColor[1];
                    gTop.bg_color[2] = glowColor[2]; gTop.bg_color[3] = glowColor[3];
                    gTop.flags = 8;
                    cellInstances.push_back(gTop);

                    // Glow below
                    D3DCellInstance gBot = {};
                    gBot.position[0] = seg.x;
                    gBot.position[1] = seg.y + seg.height;
                    gBot.atlas_size[0] = seg.width;
                    gBot.atlas_size[1] = glowThickness;
                    gBot.bg_color[0] = glowColor[0]; gBot.bg_color[1] = glowColor[1];
                    gBot.bg_color[2] = glowColor[2]; gBot.bg_color[3] = glowColor[3];
                    gBot.flags = 8;
                    cellInstances.push_back(gBot);
                } else {
                    // Vertical border -- glow left
                    D3DCellInstance gLeft = {};
                    gLeft.position[0] = seg.x - glowThickness;
                    gLeft.position[1] = seg.y;
                    gLeft.atlas_size[0] = glowThickness;
                    gLeft.atlas_size[1] = seg.height;
                    gLeft.bg_color[0] = glowColor[0]; gLeft.bg_color[1] = glowColor[1];
                    gLeft.bg_color[2] = glowColor[2]; gLeft.bg_color[3] = glowColor[3];
                    gLeft.flags = 8;
                    cellInstances.push_back(gLeft);

                    // Glow right
                    D3DCellInstance gRight = {};
                    gRight.position[0] = seg.x + seg.width;
                    gRight.position[1] = seg.y;
                    gRight.atlas_size[0] = glowThickness;
                    gRight.atlas_size[1] = seg.height;
                    gRight.bg_color[0] = glowColor[0]; gRight.bg_color[1] = glowColor[1];
                    gRight.bg_color[2] = glowColor[2]; gRight.bg_color[3] = glowColor[3];
                    gRight.flags = 8;
                    cellInstances.push_back(gRight);
                }
            }

            // Unread dot indicator: 4x4 px at the top-right corner of
            // the border segment when there are unread notifications
            // but the pane does NOT need full attention.
            if (seg.has_unread && !seg.needs_attention) {
                float dotSize = 4.0f;
                D3DCellInstance dot = {};
                dot.position[0] = seg.x + seg.width - dotSize;
                dot.position[1] = seg.y;
                dot.atlas_size[0] = dotSize;
                dot.atlas_size[1] = dotSize;
                dot.bg_color[0] = 0.0f; dot.bg_color[1] = 0.478f;
                dot.bg_color[2] = 0.8f; dot.bg_color[3] = 1.0f; // #007acc
                dot.flags = 8; // solid color rect
                cellInstances.push_back(dot);
            }
        }
    }

    // Pass 10: Sidebar panel
    buildSidebarOverlay(cellW, cellH, ascent, fontSize);
}

// Explicit template instantiations for Screen and ScreenSnapshot
template void D3DTextRenderer::Impl::buildOverlayPasses<Screen>(const Screen&, float, float, float, float);
template void D3DTextRenderer::Impl::buildOverlayPasses<::ScreenSnapshot>(const ::ScreenSnapshot&, float, float, float, float);

} // namespace termcore

#endif // _WIN32
