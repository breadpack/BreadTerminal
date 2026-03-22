#include "GLTextRendererImpl.h"

#include <string>

namespace termcore {

void GLTextRenderer::Impl::buildPaneStatusOverlays(float cellW, float cellH,
                                                    float ascent,
                                                    float fontSize) {
    const auto& statusBar = this->statusBar;

    // Per-pane progress bars: 2px bar positioned just above status bar
    if (!paneProgress.empty()) {
        float progressBarHeight = 2.0f;
        float progressY = statusBar.visible
            ? viewportHeight - cellH - progressBarHeight
            : viewportHeight - progressBarHeight;

        for (const auto& [paneId, info] : paneProgress) {
            if (info.progress < 0.0f) continue;

            float clampedProgress = std::min(1.0f, std::max(0.0f, info.progress));

            // Background track
            GLCellInstance track = {};
            track.position[0] = 0;
            track.position[1] = progressY;
            track.atlas_size[0] = viewportWidth;
            track.atlas_size[1] = progressBarHeight;
            track.bg_color[0] = 0.118f; track.bg_color[1] = 0.118f;
            track.bg_color[2] = 0.118f; track.bg_color[3] = 1.0f;
            track.flags = 8;
            track.extra_flags = 0;
            cellInstances.push_back(track);

            // Filled portion
            if (clampedProgress > 0.0f) {
                float filledWidth = viewportWidth * clampedProgress;
                float accentColor[4];
                colorFromRGBA(info.color | 0xFF000000, accentColor);

                GLCellInstance fill = {};
                fill.position[0] = 0;
                fill.position[1] = progressY;
                fill.atlas_size[0] = filledWidth;
                fill.atlas_size[1] = progressBarHeight;
                fill.bg_color[0] = accentColor[0]; fill.bg_color[1] = accentColor[1];
                fill.bg_color[2] = accentColor[2]; fill.bg_color[3] = accentColor[3];
                fill.flags = 8;
                fill.extra_flags = 0;
                cellInstances.push_back(fill);
            }

            // Label text
            if (!info.label.empty()) {
                float labelY = progressY - cellH;
                int labelCols = static_cast<int>(viewportWidth / cellW);
                int labelStart = (labelCols - static_cast<int>(info.label.size())) / 2;
                if (labelStart < 0) labelStart = 0;

                for (size_t i = 0; i < info.label.size(); ++i) {
                    int col = labelStart + static_cast<int>(i);
                    if (col >= labelCols) break;

                    char32_t cp = static_cast<char32_t>(
                        static_cast<unsigned char>(info.label[i]));
                    if (cp == ' ' || cp == 0) continue;

                    CollectionFaceId faceId = fontCollection->resolveFace(cp);
                    if (faceId == kInvalidCollectionFace) continue;
                    FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);
                    uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
                    if (glyphIdx == 0) continue;

                    GlyphKey key{rastFace, glyphIdx, {0, 0}};
                    auto glyphInfo = glyphCache->getOrRasterize(
                        key, fontSize, *rasterizer, *glyphAtlas);
                    if (!glyphInfo || glyphInfo->region.width <= 0) continue;

                    GLCellInstance inst = {};
                    float offsetX = static_cast<float>(glyphInfo->region.bearing_x);
                    float offsetY = ascent - static_cast<float>(glyphInfo->region.bearing_y);
                    inst.position[0] = col * cellW + offsetX;
                    inst.position[1] = labelY + offsetY;
                    inst.atlas_uv[0] = static_cast<float>(glyphInfo->region.x);
                    inst.atlas_uv[1] = static_cast<float>(glyphInfo->region.y);
                    inst.atlas_size[0] = static_cast<float>(glyphInfo->region.width);
                    inst.atlas_size[1] = static_cast<float>(glyphInfo->region.height);
                    inst.fg_color[0] = 0.8f; inst.fg_color[1] = 0.8f;
                    inst.fg_color[2] = 0.8f; inst.fg_color[3] = 1.0f;
                    inst.flags = 1;
                    inst.extra_flags = 0;
                    cellInstances.push_back(inst);
                }
            }
        }
    }

    // Status pills in the status bar
    if (statusBar.visible && !paneStatusPills.empty()) {
        float statusY = viewportHeight - cellH;
        int statusCols = static_cast<int>(viewportWidth / cellW);
        int pillCol = 1 + static_cast<int>(statusBar.left_text.size()) + 1;
        float pillPadding = 1.0f;
        float pillVPad = 2.0f;

        for (const auto& [paneId, pills] : paneStatusPills) {
            for (const auto& pill : pills) {
                int pillTextLen = static_cast<int>(pill.text.size());
                int pillWidth = pillTextLen + 2;

                if (pillCol + pillWidth >= statusCols) break;

                // Pill background
                GLCellInstance pillBg = {};
                pillBg.position[0] = pillCol * cellW;
                pillBg.position[1] = statusY + pillVPad;
                pillBg.atlas_size[0] = pillWidth * cellW;
                pillBg.atlas_size[1] = cellH - pillVPad * 2.0f;
                float pillBgColor[4];
                colorFromRGBA(pill.bg_color | 0xFF000000, pillBgColor);
                pillBg.bg_color[0] = pillBgColor[0]; pillBg.bg_color[1] = pillBgColor[1];
                pillBg.bg_color[2] = pillBgColor[2]; pillBg.bg_color[3] = pillBgColor[3];
                pillBg.flags = 8;
                pillBg.extra_flags = 0;
                cellInstances.push_back(pillBg);

                // Pill text
                int textCol = pillCol + 1;
                for (size_t ci = 0; ci < pill.text.size(); ++ci) {
                    int col = textCol + static_cast<int>(ci);
                    if (col >= statusCols) break;

                    char32_t cp = static_cast<char32_t>(
                        static_cast<unsigned char>(pill.text[ci]));
                    if (cp == ' ' || cp == 0) continue;

                    CollectionFaceId faceId = fontCollection->resolveFace(cp);
                    if (faceId == kInvalidCollectionFace) continue;
                    FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);
                    uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
                    if (glyphIdx == 0) continue;

                    GlyphKey key{rastFace, glyphIdx, {0, 0}};
                    auto glyphInfo = glyphCache->getOrRasterize(
                        key, fontSize, *rasterizer, *glyphAtlas);
                    if (!glyphInfo || glyphInfo->region.width <= 0) continue;

                    GLCellInstance inst = {};
                    float offsetX = static_cast<float>(glyphInfo->region.bearing_x);
                    float offsetY = ascent - static_cast<float>(glyphInfo->region.bearing_y);
                    inst.position[0] = col * cellW + offsetX;
                    inst.position[1] = statusY + offsetY;
                    inst.atlas_uv[0] = static_cast<float>(glyphInfo->region.x);
                    inst.atlas_uv[1] = static_cast<float>(glyphInfo->region.y);
                    inst.atlas_size[0] = static_cast<float>(glyphInfo->region.width);
                    inst.atlas_size[1] = static_cast<float>(glyphInfo->region.height);
                    float pillFgColor[4];
                    colorFromRGBA(pill.fg_color | 0xFF000000, pillFgColor);
                    inst.fg_color[0] = pillFgColor[0]; inst.fg_color[1] = pillFgColor[1];
                    inst.fg_color[2] = pillFgColor[2]; inst.fg_color[3] = pillFgColor[3];
                    inst.flags = 1;
                    inst.extra_flags = 0;
                    cellInstances.push_back(inst);
                }

                pillCol += pillWidth + static_cast<int>(pillPadding);
            }
        }
    }
}

} // namespace termcore
