#include "GLTextRendererImpl.h"
#include "termcore/font/box_drawing.h"

namespace termcore {

void GLTextRenderer::Impl::colorFromRGBA(uint32_t rgba, float out[4]) {
    out[0] = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
    out[1] = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
    out[2] = static_cast<float>(rgba & 0xFF) / 255.0f;
    out[3] = 1.0f;
}

bool GLTextRenderer::Impl::isCellSelected(int row, int col) const {
    if (!selection.active) return false;
    int sr = selection.startRow, sc = selection.startCol;
    int er = selection.endRow, ec = selection.endCol;
    if (sr > er || (sr == er && sc > ec)) {
        std::swap(sr, er);
        std::swap(sc, ec);
    }
    if (row < sr || row > er) return false;
    if (row == sr && row == er) return col >= sc && col <= ec;
    if (row == sr) return col >= sc;
    if (row == er) return col <= ec;
    return true;
}

void GLTextRenderer::Impl::rebuildSearchIndex() {
    searchByRow.clear();
    for (int i = 0; i < static_cast<int>(searchHighlights.size()); ++i) {
        searchByRow[searchHighlights[i].row].push_back({i, i});
    }
}

int GLTextRenderer::Impl::searchHighlightType(int row, int col) const {
    auto it = searchByRow.find(row);
    if (it == searchByRow.end()) return 0;
    for (const auto& [idx, _] : it->second) {
        const auto& h = searchHighlights[idx];
        if (col >= h.startCol && col < h.endCol) {
            return (idx == searchCurrentIndex) ? 2 : 1;
        }
    }
    return 0;
}

void GLTextRenderer::Impl::buildCellBuffer(const Screen& screen) {
    if (!fontCollection || !glyphCache || !glyphAtlas || !rasterizer) {
        return;
    }

    FontMetrics metrics = fontCollection->primaryMetrics();
    float cellW = metrics.cell_width;
    float cellH = metrics.cell_height;
    float ascent = metrics.ascent;
    float fontSize = fontCollection->fontSize();

    int rows = screen.rows();
    int cols = screen.cols();

    const DynamicColors& colors = screen.dynamicColors();

    cellInstances.clear();
    cellInstances.reserve(rows * cols * 2);

    // Offset grid down when tab bar is visible
    float tabBarH = cellH * GLTextRenderer::kTabBarHeightScale;
    float gridOffsetY = (tabBar.visible && !tabBar.tabs.empty()) ? tabBarH : 0.0f;

    // Pass 1: Background quads (cell-sized)
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            GLCellInstance inst = {};
            inst.position[0] = col * cellW;
            inst.position[1] = row * cellH + gridOffsetY;

            colorFromRGBA(colors.resolveBg(cell.bg_color), inst.bg_color);
            colorFromRGBA(colors.resolveFg(cell.fg_color), inst.fg_color);

            if (cell.attributes & AttrInverse) {
                std::swap(inst.fg_color[0], inst.bg_color[0]);
                std::swap(inst.fg_color[1], inst.bg_color[1]);
                std::swap(inst.fg_color[2], inst.bg_color[2]);
                std::swap(inst.fg_color[3], inst.bg_color[3]);
            }

            if (isCellSelected(row, col)) {
                std::swap(inst.fg_color[0], inst.bg_color[0]);
                std::swap(inst.fg_color[1], inst.bg_color[1]);
                std::swap(inst.fg_color[2], inst.bg_color[2]);
                std::swap(inst.fg_color[3], inst.bg_color[3]);
            }

            if (cell.attributes & AttrDim) {
                inst.fg_color[0] *= 0.5f;
                inst.fg_color[1] *= 0.5f;
                inst.fg_color[2] *= 0.5f;
            }

            int sht = searchHighlightType(row, col);
            if (sht == 2) {
                inst.bg_color[0] = 1.0f; inst.bg_color[1] = 0.6f;
                inst.bg_color[2] = 0.0f; inst.bg_color[3] = 1.0f;
                inst.fg_color[0] = 0.0f; inst.fg_color[1] = 0.0f;
                inst.fg_color[2] = 0.0f; inst.fg_color[3] = 1.0f;
            } else if (sht == 1) {
                inst.bg_color[0] = 1.0f; inst.bg_color[1] = 1.0f;
                inst.bg_color[2] = 0.0f; inst.bg_color[3] = 1.0f;
                inst.fg_color[0] = 0.0f; inst.fg_color[1] = 0.0f;
                inst.fg_color[2] = 0.0f; inst.fg_color[3] = 1.0f;
            }

            // Apply background opacity (premultiplied alpha) only to cells
            // with the default background color. Cells with explicit ANSI
            // background colors (e.g. prompt segments) stay fully opaque.
            bool hasDefaultBg = (cell.bg_color == kColorDefault);
            if (cell.attributes & AttrInverse) hasDefaultBg = (cell.fg_color == kColorDefault);
            if (sht == 0 && !isCellSelected(row, col) && hasDefaultBg) {
                float a = backgroundOpacity;
                inst.bg_color[0] *= a;
                inst.bg_color[1] *= a;
                inst.bg_color[2] *= a;
                inst.bg_color[3] *= a;
            }

            inst.flags = 4;  // is_bg_pass
            inst.extra_flags = 0;
            cellInstances.push_back(inst);
        }
    }

    // Pass 1b: Margin quads — fill gaps between cell grid and viewport edges
    // so margins have the same opacity as the cell background.
    {
        float gridRight  = cols * cellW;
        float gridBottom = rows * cellH + gridOffsetY;

        float defaultBg[4];
        colorFromRGBA(colors.resolveBg(colors.background), defaultBg);
        float a = backgroundOpacity;
        defaultBg[0] *= a;
        defaultBg[1] *= a;
        defaultBg[2] *= a;
        defaultBg[3] *= a;

        // Right margin (starts below tab bar so it doesn't cover tab bar with semi-transparent bg)
        if (gridRight < viewportWidth) {
            GLCellInstance inst = {};
            inst.position[0] = gridRight;
            inst.position[1] = gridOffsetY;
            inst.atlas_size[0] = viewportWidth - gridRight;
            inst.atlas_size[1] = viewportHeight - gridOffsetY;
            inst.bg_color[0] = defaultBg[0];
            inst.bg_color[1] = defaultBg[1];
            inst.bg_color[2] = defaultBg[2];
            inst.bg_color[3] = defaultBg[3];
            inst.flags = 8;  // render as rect
            inst.extra_flags = 0;
            cellInstances.push_back(inst);
        }

        // Bottom margin
        if (gridBottom < viewportHeight) {
            GLCellInstance inst = {};
            inst.position[0] = 0.0f;
            inst.position[1] = gridBottom;
            inst.atlas_size[0] = gridRight;  // only up to grid width (right margin covers rest)
            inst.atlas_size[1] = viewportHeight - gridBottom;
            inst.bg_color[0] = defaultBg[0];
            inst.bg_color[1] = defaultBg[1];
            inst.bg_color[2] = defaultBg[2];
            inst.bg_color[3] = defaultBg[3];
            inst.flags = 8;  // render as rect
            inst.extra_flags = 0;
            cellInstances.push_back(inst);
        }

        // Top margin (tab bar area): fill with same bg+opacity as terminal cells
        // so transparency looks consistent across the entire window.
        if (gridOffsetY > 0.0f) {
            GLCellInstance inst = {};
            inst.position[0] = 0.0f;
            inst.position[1] = 0.0f;
            inst.atlas_size[0] = viewportWidth;
            inst.atlas_size[1] = gridOffsetY;
            inst.bg_color[0] = defaultBg[0];
            inst.bg_color[1] = defaultBg[1];
            inst.bg_color[2] = defaultBg[2];
            inst.bg_color[3] = defaultBg[3];
            inst.flags = 8;
            inst.extra_flags = 0;
            cellInstances.push_back(inst);
        }
    }

    // Cursor position for ligature breaking
    int cursorRow = screen.cursorRow();
    int cursorCol = screen.cursorCol();

    // Pass 2: Glyph quads (glyph-sized, positioned with bearing)
    for (int row = 0; row < rows; ++row) {

        // --- Ligature detection for this row ---
        std::vector<int> ligatureSpanIndex(cols, -1);
        std::vector<LigatureShapingResult> ligatureResults;

        if (fontLigatures && fontCollection) {
            std::vector<uint32_t> rowCodepoints;
            rowCodepoints.reserve(cols);
            for (int c = 0; c < cols; ++c) {
                const TermCell& tc = screen.cellAt(row, c);
                rowCodepoints.push_back(
                    (tc.codepoint != 0) ? static_cast<uint32_t>(tc.codepoint) : ' ');
            }

            auto spans = ligatureDetector.detectLigatures(rowCodepoints, 0);

            for (const auto& span : spans) {
                if (row == cursorRow &&
                    cursorCol >= span.start_col && cursorCol < span.end_col) {
                    continue;
                }

                auto faceId = fontCollection->resolveFace(
                    static_cast<char32_t>(span.codepoints[0]));
                if (faceId == kInvalidCollectionFace) continue;

                FontFaceId shaperFace = fontCollection->shaperFaceId(faceId);
                if (shaperFace == kInvalidFontFace) continue;

                auto result = LigatureDetector::shapeLigature(
                    fontCollection->shaper(), shaperFace, span);

                if (result.glyphs.empty()) continue;

                int spanIdx = static_cast<int>(ligatureResults.size());
                ligatureResults.push_back(std::move(result));

                for (int c = span.start_col; c < span.end_col && c < cols; ++c) {
                    ligatureSpanIndex[c] = spanIdx;
                }
            }
        }

        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            // --- Handle ligature span cells ---
            if (ligatureSpanIndex[col] >= 0) {
                int spanIdx = ligatureSpanIndex[col];
                const auto& ligResult = ligatureResults[spanIdx];

                bool isFirstCol = (col == 0 || ligatureSpanIndex[col - 1] != spanIdx);
                if (!isFirstCol) continue;

                if (ligResult.glyphs.empty()) continue;
                const auto& shapedGlyph = ligResult.glyphs[0];

                auto faceId = fontCollection->resolveFace(cell.codepoint);
                if (faceId == kInvalidCollectionFace) continue;
                FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);

                GlyphKey key{rastFace, shapedGlyph.glyph_index, {0, 0}};
                auto info = glyphCache->getOrRasterize(
                    key, fontSize, *rasterizer, *glyphAtlas);
                if (!info || info->region.width <= 0 ||
                    info->region.height <= 0) {
                    continue;
                }

                GLCellInstance inst = {};

                float offsetX = static_cast<float>(info->region.bearing_x);
                float offsetY = ascent -
                    static_cast<float>(info->region.bearing_y);

                inst.position[0] = col * cellW + offsetX;
                inst.position[1] = row * cellH + offsetY + gridOffsetY;

                inst.atlas_uv[0] = static_cast<float>(info->region.x);
                inst.atlas_uv[1] = static_cast<float>(info->region.y);
                inst.atlas_size[0] = static_cast<float>(info->region.width);
                inst.atlas_size[1] = static_cast<float>(info->region.height);

                colorFromRGBA(colors.resolveFg(cell.fg_color), inst.fg_color);
                colorFromRGBA(colors.resolveBg(cell.bg_color), inst.bg_color);

                if (cell.attributes & AttrInverse) {
                    std::swap(inst.fg_color[0], inst.bg_color[0]);
                    std::swap(inst.fg_color[1], inst.bg_color[1]);
                    std::swap(inst.fg_color[2], inst.bg_color[2]);
                    std::swap(inst.fg_color[3], inst.bg_color[3]);
                }

                if (isCellSelected(row, col)) {
                    std::swap(inst.fg_color[0], inst.bg_color[0]);
                    std::swap(inst.fg_color[1], inst.bg_color[1]);
                    std::swap(inst.fg_color[2], inst.bg_color[2]);
                    std::swap(inst.fg_color[3], inst.bg_color[3]);
                }

                if (cell.attributes & AttrDim) {
                    inst.fg_color[0] *= 0.5f;
                    inst.fg_color[1] *= 0.5f;
                    inst.fg_color[2] *= 0.5f;
                }

                int sht2 = searchHighlightType(row, col);
                if (sht2 > 0) {
                    inst.fg_color[0] = 0.0f; inst.fg_color[1] = 0.0f;
                    inst.fg_color[2] = 0.0f; inst.fg_color[3] = 1.0f;
                }

                inst.flags = 1;  // has_glyph
                if (info->is_color) inst.flags |= 2;
                inst.extra_flags = 0;

                cellInstances.push_back(inst);
                continue;
            }

            if (cell.codepoint == ' ' || cell.codepoint == 0) {
                continue;
            }
            if (cell.width == 0) continue;  // Skip continuation cells

            char32_t cp = cell.codepoint;

            // Procedural box drawing / powerline glyphs
            if (is_box_drawing(cp)) {
                GlyphKey boxKey{kInvalidFontFace, static_cast<uint32_t>(cp), {0, 0}};
                auto boxInfo = glyphCache->get(boxKey);
                if (!boxInfo) {
                    BoxGlyphBitmap boxBitmap = render_box_glyph(
                        cp,
                        static_cast<int>(cellW),
                        static_cast<int>(cellH));
                    if (!boxBitmap.bitmap.empty()) {
                        RasterizedGlyph rg;
                        rg.bitmap = std::move(boxBitmap.bitmap);
                        rg.width = boxBitmap.width;
                        rg.height = boxBitmap.height;
                        rg.bearing_x = 0;
                        rg.bearing_y = static_cast<int32_t>(ascent);
                        rg.format = PixelFormat::Grayscale;
                        auto region = glyphAtlas->pack(rg);
                        if (region) {
                            GlyphInfo gi;
                            gi.region = *region;
                            gi.advance_x = cellW;
                            gi.advance_y = 0;
                            gi.is_color = false;
                            glyphCache->put(boxKey, gi);
                            boxInfo = gi;
                        }
                    }
                }
                if (boxInfo) {
                    GLCellInstance inst = {};
                    inst.position[0] = col * cellW;
                    inst.position[1] = row * cellH + gridOffsetY;
                    inst.atlas_uv[0] = static_cast<float>(boxInfo->region.x);
                    inst.atlas_uv[1] = static_cast<float>(boxInfo->region.y);
                    inst.atlas_size[0] = static_cast<float>(boxInfo->region.width);
                    inst.atlas_size[1] = static_cast<float>(boxInfo->region.height);

                    colorFromRGBA(colors.resolveFg(cell.fg_color), inst.fg_color);
                    colorFromRGBA(colors.resolveBg(cell.bg_color), inst.bg_color);
                    if (cell.attributes & AttrInverse) {
                        std::swap(inst.fg_color[0], inst.bg_color[0]);
                        std::swap(inst.fg_color[1], inst.bg_color[1]);
                        std::swap(inst.fg_color[2], inst.bg_color[2]);
                        std::swap(inst.fg_color[3], inst.bg_color[3]);
                    }
                    if (isCellSelected(row, col)) {
                        std::swap(inst.fg_color[0], inst.bg_color[0]);
                        std::swap(inst.fg_color[1], inst.bg_color[1]);
                        std::swap(inst.fg_color[2], inst.bg_color[2]);
                        std::swap(inst.fg_color[3], inst.bg_color[3]);
                    }
                    if (cell.attributes & AttrDim) {
                        inst.fg_color[0] *= 0.5f;
                        inst.fg_color[1] *= 0.5f;
                        inst.fg_color[2] *= 0.5f;
                    }
                    inst.flags = 1;  // has_glyph
                    inst.extra_flags = 0;
                    cellInstances.push_back(inst);
                }
                continue;
            }

            CollectionFaceId faceId;
            FontFaceId rastFace;
            uint32_t glyphIdx;

            if (cell.extra_count > 0) {
                auto shaped = fontCollection->shapeCluster(cell.allCodepoints());
                faceId = shaped.face;
                if (faceId == kInvalidCollectionFace) continue;
                rastFace = fontCollection->rasterizerFaceId(faceId);
                glyphIdx = shaped.glyph_index;
            } else {
                faceId = fontCollection->resolveFace(cell.codepoint);
                if (faceId == kInvalidCollectionFace) continue;
                rastFace = fontCollection->rasterizerFaceId(faceId);
                glyphIdx = rasterizer->getGlyphIndex(rastFace, cell.codepoint);
            }
            if (glyphIdx == 0) continue;

            GlyphKey key{rastFace, glyphIdx, {0, 0}};
            auto info = glyphCache->getOrRasterize(
                key, fontSize, *rasterizer, *glyphAtlas);
            if (!info || info->region.width <= 0 ||
                info->region.height <= 0) {
                continue;
            }

            GLCellInstance inst = {};

            float offsetX = static_cast<float>(info->region.bearing_x);
            float offsetY = ascent -
                static_cast<float>(info->region.bearing_y);

            inst.position[0] = col * cellW + offsetX;
            inst.position[1] = row * cellH + offsetY + gridOffsetY;

            inst.atlas_uv[0] = static_cast<float>(info->region.x);
            inst.atlas_uv[1] = static_cast<float>(info->region.y);
            inst.atlas_size[0] = static_cast<float>(info->region.width);
            inst.atlas_size[1] = static_cast<float>(info->region.height);

            colorFromRGBA(colors.resolveFg(cell.fg_color), inst.fg_color);
            colorFromRGBA(colors.resolveBg(cell.bg_color), inst.bg_color);

            if (cell.attributes & AttrInverse) {
                std::swap(inst.fg_color[0], inst.bg_color[0]);
                std::swap(inst.fg_color[1], inst.bg_color[1]);
                std::swap(inst.fg_color[2], inst.bg_color[2]);
                std::swap(inst.fg_color[3], inst.bg_color[3]);
            }

            if (isCellSelected(row, col)) {
                std::swap(inst.fg_color[0], inst.bg_color[0]);
                std::swap(inst.fg_color[1], inst.bg_color[1]);
                std::swap(inst.fg_color[2], inst.bg_color[2]);
                std::swap(inst.fg_color[3], inst.bg_color[3]);
            }

            if (cell.attributes & AttrDim) {
                inst.fg_color[0] *= 0.5f;
                inst.fg_color[1] *= 0.5f;
                inst.fg_color[2] *= 0.5f;
            }

            int sht2 = searchHighlightType(row, col);
            if (sht2 > 0) {
                inst.fg_color[0] = 0.0f; inst.fg_color[1] = 0.0f;
                inst.fg_color[2] = 0.0f; inst.fg_color[3] = 1.0f;
            }

            inst.flags = 1;  // has_glyph
            if (info->is_color) inst.flags |= 2;
            inst.extra_flags = 0;

            cellInstances.push_back(inst);
        }
    }

    // Pass 2b: Ghost text (dim suggestion text after cursor)
    if (!ghostText.text.empty() && ghostText.row >= 0 && ghostText.col >= 0 &&
        ghostText.row < rows) {
        const DynamicColors& gtColors = screen.dynamicColors();
        int gtCol = ghostText.col;

        // Decode UTF-8 ghost text to codepoints and render each
        const std::string& gt = ghostText.text;
        size_t i = 0;
        while (i < gt.size() && gtCol < cols) {
            // UTF-8 decode
            char32_t cp = 0;
            uint8_t b = static_cast<uint8_t>(gt[i]);
            int seqLen = 1;
            if (b < 0x80) { cp = b; }
            else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; seqLen = 2; }
            else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; seqLen = 3; }
            else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; seqLen = 4; }
            for (int j = 1; j < seqLen && (i + j) < gt.size(); ++j) {
                cp = (cp << 6) | (static_cast<uint8_t>(gt[i + j]) & 0x3F);
            }
            i += seqLen;

            if (cp == ' ' || cp == 0) { ++gtCol; continue; }

            // Check if cell already has content — don't overlay
            const TermCell& existing = screen.cellAt(ghostText.row, gtCol);
            if (existing.codepoint != ' ' && existing.codepoint != 0) {
                break;
            }

            // Look up glyph
            auto faceId = fontCollection->resolveFace(cp);
            if (faceId == kInvalidCollectionFace) { ++gtCol; continue; }
            auto rastFace = fontCollection->rasterizerFaceId(faceId);
            uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
            if (glyphIdx == 0) { ++gtCol; continue; }

            GlyphKey key{rastFace, glyphIdx, {0, 0}};
            auto info = glyphCache->getOrRasterize(key, fontSize, *rasterizer, *glyphAtlas);
            if (!info || info->region.width <= 0 || info->region.height <= 0) {
                ++gtCol; continue;
            }

            GLCellInstance inst = {};
            float offsetX = static_cast<float>(info->region.bearing_x);
            float offsetY = ascent - static_cast<float>(info->region.bearing_y);

            inst.position[0] = gtCol * cellW + offsetX;
            inst.position[1] = ghostText.row * cellH + offsetY + gridOffsetY;
            inst.atlas_uv[0] = static_cast<float>(info->region.x);
            inst.atlas_uv[1] = static_cast<float>(info->region.y);
            inst.atlas_size[0] = static_cast<float>(info->region.width);
            inst.atlas_size[1] = static_cast<float>(info->region.height);

            // Ghost text color: 20% brightness of default foreground
            float fgFull[4];
            colorFromRGBA(gtColors.resolveFg(kColorDefault), fgFull);
            inst.fg_color[0] = fgFull[0] * 0.20f;
            inst.fg_color[1] = fgFull[1] * 0.20f;
            inst.fg_color[2] = fgFull[2] * 0.20f;
            inst.fg_color[3] = fgFull[3];

            inst.flags = 1;  // has_glyph
            inst.extra_flags = 0;
            cellInstances.push_back(inst);
            ++gtCol;
        }
    }

    // Pass 3: Underlines
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            uint8_t ulStyle = cell.underline_style;
            if (ulStyle == 0 && (cell.attributes & AttrUnderline)) {
                ulStyle = 1;
            }
            if (ulStyle == 0) continue;

            float ulColor[4];
            if (cell.underline_color != kColorDefault) {
                colorFromRGBA(colors.resolveFg(cell.underline_color), ulColor);
            } else {
                colorFromRGBA(colors.resolveFg(cell.fg_color), ulColor);
            }

            if (cell.attributes & AttrInverse) {
                colorFromRGBA(colors.resolveBg(cell.bg_color), ulColor);
                if (cell.underline_color != kColorDefault) {
                    colorFromRGBA(colors.resolveFg(cell.underline_color), ulColor);
                }
            }

            auto pushUnderlineRect = [&](float y, float h, uint32_t flags, uint32_t extraFlags) {
                GLCellInstance inst = {};
                inst.position[0] = col * cellW;
                inst.position[1] = y;
                inst.atlas_size[0] = cellW;
                inst.atlas_size[1] = h;
                inst.bg_color[0] = ulColor[0];
                inst.bg_color[1] = ulColor[1];
                inst.bg_color[2] = ulColor[2];
                inst.bg_color[3] = ulColor[3];
                inst.flags = flags;
                inst.extra_flags = extraFlags;
                cellInstances.push_back(inst);
            };

            float baseY = row * cellH + gridOffsetY;

            switch (ulStyle) {
                case 1: // single
                    pushUnderlineRect(baseY + cellH - 2.0f, 1.0f, 8, 1);
                    break;
                case 2: // double
                    pushUnderlineRect(baseY + cellH - 3.0f, 1.0f, 8, 2);
                    pushUnderlineRect(baseY + cellH - 1.0f, 1.0f, 8, 2);
                    break;
                case 3: // curly
                    pushUnderlineRect(baseY + cellH - 4.0f, 3.0f, 16, 3);
                    break;
                case 4: // dotted
                    pushUnderlineRect(baseY + cellH - 2.0f, 1.0f, 16, 4);
                    break;
                case 5: // dashed
                    pushUnderlineRect(baseY + cellH - 2.0f, 1.0f, 16, 5);
                    break;
                default:
                    break;
            }
        }
    }

    // Record insertion point before cursor instances
    cellCountBeforeCursor = cellInstances.size();

    // Pass 4: Cursor
    appendCursorInstances(screen, cellW, cellH, gridOffsetY);

    // Passes 5-9: Overlay elements
    buildOverlayPasses(screen, cellW, cellH, ascent, fontSize);
}

void GLTextRenderer::Impl::appendCursorInstances(
        const Screen& screen, float cellW, float cellH, float gridOffsetY) {
    int rows = screen.rows();
    int cols = screen.cols();

    if (screen.cursorVisible() && cursorBlinkVisible && screen.viewportOffset() == 0) {
        int cRow = screen.cursorRow();
        int cCol = screen.cursorCol();
        if (cRow >= 0 && cRow < rows && cCol >= 0 && cCol < cols) {
            const DynamicColors& colors = screen.dynamicColors();
            uint32_t cursorColor =
                colors.resolveFg(colors.cursor_color);

            CursorShape shape = screen.cursorShape();

            if (shape == CursorShape::Block) {
                GLCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH + gridOffsetY;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.bg_color[3] = 0.5f;
                inst.flags = 4;  // is_bg_pass
                inst.extra_flags = 0;
                cellInstances.push_back(inst);
            } else if (shape == CursorShape::Bar) {
                GLCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH + gridOffsetY;
                inst.atlas_size[0] = 2.0f;
                inst.atlas_size[1] = cellH;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.flags = 8;  // is_cursor
                inst.extra_flags = 0;
                cellInstances.push_back(inst);
            } else if (shape == CursorShape::Underline) {
                GLCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH + gridOffsetY + cellH - 2.0f;
                inst.atlas_size[0] = cellW;
                inst.atlas_size[1] = 2.0f;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.flags = 8;  // is_cursor
                inst.extra_flags = 0;
                cellInstances.push_back(inst);
            }
        }
    }
}

void GLTextRenderer::Impl::patchCursorOnly(const Screen& screen) {
    if (!fontCollection) return;

    FontMetrics metrics = fontCollection->primaryMetrics();
    float cellW = metrics.cell_width;
    float cellH = metrics.cell_height;

    float tabBarH = cellH * GLTextRenderer::kTabBarHeightScale;
    float gridOffsetY = (tabBar.visible && !tabBar.tabs.empty()) ? tabBarH : 0.0f;

    // Remove old cursor instances
    cellInstances.resize(cellCountBeforeCursor);

    // Append fresh cursor instances
    appendCursorInstances(screen, cellW, cellH, gridOffsetY);

    // Re-append overlay passes
    float ascent = metrics.ascent;
    float fontSize = fontCollection->fontSize();
    buildOverlayPasses(screen, cellW, cellH, ascent, fontSize);
}

} // namespace termcore
