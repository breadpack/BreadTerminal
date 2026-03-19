#if defined(_WIN32)

#include "D3DTextRendererImpl.h"

#include <string>

namespace termcore {

void D3DTextRenderer::Impl::buildOverlayPasses(const Screen& screen,
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

                D3DCellInstance inst = {};
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

            D3DCellInstance inst = {};
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
            inst.flags = 1;  // has_glyph
            cellInstances.push_back(inst);
        }
    }

    // Pass 8: Tab Bar (with notification indicators)
    const auto& tabBar = this->tabBar;
    if (tabBar.visible && !tabBar.tabs.empty()) {
        float tabBarY = 0.0f;
        int totalCols = static_cast<int>(viewportWidth / cellW);

        // Full-width tab bar background
        D3DCellInstance tabBarBg = {};
        tabBarBg.position[0] = 0;
        tabBarBg.position[1] = tabBarY;
        tabBarBg.atlas_size[0] = viewportWidth;
        tabBarBg.atlas_size[1] = cellH;
        colorFromRGBA(tabBar.bg_color | 0xFF000000, tabBarBg.bg_color);
        tabBarBg.flags = 4;  // is_bg_pass
        cellInstances.push_back(tabBarBg);

        // Each tab gets an equal share of columns, capped at a reasonable width
        int tabCount = static_cast<int>(tabBar.tabs.size());
        int maxTabWidth = 20;  // max columns per tab
        int tabWidth = (std::min)(maxTabWidth, totalCols / (std::max)(1, tabCount));

        for (int t = 0; t < tabCount; ++t) {
            const auto& tab = tabBar.tabs[t];
            int tabStartCol = t * tabWidth;
            if (tabStartCol >= totalCols) break;

            uint32_t tabBgColor = tab.active
                ? tabBar.active_bg_color
                : tabBar.inactive_bg_color;

            // Override background for tabs needing attention (accent blue).
            if (tab.needs_attention) {
                tabBgColor = 0x007acc;
            }

            // Tab background
            D3DCellInstance tabBg = {};
            tabBg.position[0] = tabStartCol * cellW;
            tabBg.position[1] = tabBarY;
            tabBg.atlas_size[0] = tabWidth * cellW;
            tabBg.atlas_size[1] = cellH;
            colorFromRGBA(tabBgColor | 0xFF000000, tabBg.bg_color);
            tabBg.flags = 4;  // is_bg_pass
            cellInstances.push_back(tabBg);

            // Tab title text (with 1-col padding)
            const std::string& title = tab.title;
            int textStart = tabStartCol + 1;
            int textEnd = tabStartCol + tabWidth - 1;
            for (size_t i = 0; i < title.size(); ++i) {
                int col = textStart + static_cast<int>(i);
                if (col >= textEnd || col >= totalCols) break;

                char32_t cp = static_cast<char32_t>(
                    static_cast<unsigned char>(title[i]));
                if (cp == ' ' || cp == 0) continue;

                CollectionFaceId faceId = fontCollection->resolveFace(cp);
                if (faceId == kInvalidCollectionFace) continue;
                FontFaceId rastFace =
                    fontCollection->rasterizerFaceId(faceId);
                uint32_t glyphIdx =
                    rasterizer->getGlyphIndex(rastFace, cp);
                if (glyphIdx == 0) continue;

                GlyphKey key{rastFace, glyphIdx, {0, 0}};
                auto info = glyphCache->getOrRasterize(
                    key, fontSize, *rasterizer, *glyphAtlas);
                if (!info || info->region.width <= 0) continue;

                D3DCellInstance inst = {};
                float offsetX =
                    static_cast<float>(info->region.bearing_x);
                float offsetY =
                    ascent - static_cast<float>(info->region.bearing_y);
                inst.position[0] = col * cellW + offsetX;
                inst.position[1] = tabBarY + offsetY;
                inst.atlas_uv[0] =
                    static_cast<float>(info->region.x);
                inst.atlas_uv[1] =
                    static_cast<float>(info->region.y);
                inst.atlas_size[0] =
                    static_cast<float>(info->region.width);
                inst.atlas_size[1] =
                    static_cast<float>(info->region.height);
                colorFromRGBA(tabBar.fg_color | 0xFF000000,
                              inst.fg_color);
                inst.flags = 1;  // has_glyph
                cellInstances.push_back(inst);
            }

            // Unread dot indicator: small 3px radius circle after tab title.
            if (tab.has_unread && !tab.active) {
                int dotCol = textStart + static_cast<int>(title.size());
                if (dotCol < textEnd && dotCol < totalCols) {
                    float dotRadius = 3.0f;
                    float dotX = dotCol * cellW + cellW * 0.5f - dotRadius;
                    float dotY = tabBarY + cellH * 0.5f - dotRadius;

                    D3DCellInstance dot = {};
                    dot.position[0] = dotX;
                    dot.position[1] = dotY;
                    dot.atlas_size[0] = dotRadius * 2.0f;
                    dot.atlas_size[1] = dotRadius * 2.0f;
                    dot.bg_color[0] = 0.0f; dot.bg_color[1] = 0.478f;
                    dot.bg_color[2] = 0.8f; dot.bg_color[3] = 1.0f; // #007acc
                    dot.flags = 8; // solid color rect
                    cellInstances.push_back(dot);
                }
            }
        }
    }

    // Pane progress bars and status pills (delegated to D3DCellBuilderPaneStatus.cpp)
    buildPaneStatusOverlays(cellW, cellH, ascent, fontSize);

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

            // Notification glow border: 2px wide overlay in ring_color
            // with pulsing alpha when agent needs attention.
            if (seg.needs_attention && seg.ring_intensity > 0.0f) {
                float glowThickness = 2.0f;
                float glowColor[4];
                colorFromRGBA(seg.ring_color | 0xFF000000, glowColor);
                glowColor[3] = seg.ring_intensity; // pulse alpha

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
}

} // namespace termcore

#endif // _WIN32
