#if defined(_WIN32)

#include "D3DTextRendererImpl.h"
#include "termcore/font/box_drawing.h"

namespace termcore {

void D3DTextRenderer::Impl::colorFromRGBA(uint32_t rgba, float out[4]) {
    out[0] = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
    out[1] = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
    out[2] = static_cast<float>(rgba & 0xFF) / 255.0f;
    out[3] = 1.0f;
}

bool D3DTextRenderer::Impl::isCellSelected(int row, int col) const {
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

int D3DTextRenderer::Impl::searchHighlightType(int row, int col) const {
    for (int i = 0; i < static_cast<int>(searchHighlights.size()); ++i) {
        const auto& h = searchHighlights[i];
        if (h.row == row && col >= h.startCol && col < h.endCol) {
            return (i == searchCurrentIndex) ? 2 : 1;
        }
    }
    return 0;
}

const D3DTextRenderer::UrlHighlight* D3DTextRenderer::Impl::urlHighlightAt(int row, int col) const {
    for (const auto& h : urlHighlights) {
        if (h.row == row && col >= h.startCol && col < h.endCol) {
            return &h;
        }
    }
    return nullptr;
}

void D3DTextRenderer::Impl::buildCellBuffer(const Screen& screen) {
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
    float tabBarH = cellH * D3DTextRenderer::kTabBarHeightScale;
    float gridOffsetY = (tabBar.visible && !tabBar.tabs.empty()) ? tabBarH : 0.0f;

    // Pass 1: Background quads (cell-sized)
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            D3DCellInstance inst = {};
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

            bool selected = isCellSelected(row, col);
            if (selected) {
                std::swap(inst.fg_color[0], inst.bg_color[0]);
                std::swap(inst.fg_color[1], inst.bg_color[1]);
                std::swap(inst.fg_color[2], inst.bg_color[2]);
                std::swap(inst.fg_color[3], inst.bg_color[3]);
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
            if (sht == 0 && !selected && hasDefaultBg) {
                float a = backgroundOpacity;
                inst.bg_color[0] *= a;
                inst.bg_color[1] *= a;
                inst.bg_color[2] *= a;
                inst.bg_color[3] *= a;
            }

            inst.flags = 4;  // is_bg_pass
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

        // Right margin
        if (gridRight < viewportWidth) {
            D3DCellInstance inst = {};
            inst.position[0] = gridRight;
            inst.position[1] = 0.0f;
            inst.atlas_size[0] = viewportWidth - gridRight;
            inst.atlas_size[1] = viewportHeight;
            inst.bg_color[0] = defaultBg[0];
            inst.bg_color[1] = defaultBg[1];
            inst.bg_color[2] = defaultBg[2];
            inst.bg_color[3] = defaultBg[3];
            inst.flags = 8;  // render as rect (cursor/underline path)
            cellInstances.push_back(inst);
        }

        // Bottom margin
        if (gridBottom < viewportHeight) {
            D3DCellInstance inst = {};
            inst.position[0] = 0.0f;
            inst.position[1] = gridBottom;
            inst.atlas_size[0] = gridRight;  // only up to grid width (right margin covers rest)
            inst.atlas_size[1] = viewportHeight - gridBottom;
            inst.bg_color[0] = defaultBg[0];
            inst.bg_color[1] = defaultBg[1];
            inst.bg_color[2] = defaultBg[2];
            inst.bg_color[3] = defaultBg[3];
            inst.flags = 8;  // render as rect
            cellInstances.push_back(inst);
        }

        // Top margin (above grid, when no tab bar)
        if (gridOffsetY > 0.0f) {
            // Tab bar area is handled by overlay passes; if no tab bar but
            // there's an offset, fill it.
        }
    }

    // Pass 2: Glyph quads (glyph-sized, positioned with bearing)
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            if (cell.codepoint == ' ' || cell.codepoint == 0) {
                continue;
            }
            if (cell.width == 0) continue;  // Skip continuation cells

            char32_t cp = cell.codepoint;

            // Powerline glyphs (E0B0-E0B3): try font first, procedural fallback
            bool isPowerline = (cp >= 0xE0B0 && cp <= 0xE0B3);
            // Non-powerline box drawing: always procedural
            bool isBoxDrawing = !isPowerline && is_box_drawing(cp);

            if (isBoxDrawing) {
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
                    D3DCellInstance inst = {};
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
                    inst.flags = 1;  // has_glyph
                    cellInstances.push_back(inst);
                }
                continue;
            }

            CollectionFaceId faceId;
            FontFaceId rastFace;
            uint32_t glyphIdx;
            bool fontHasGlyph = false;

            if (cell.extra_count > 0) {
                // Multi-codepoint grapheme cluster: shape via HarfBuzz
                auto shaped = fontCollection->shapeCluster(cell.allCodepoints());
                faceId = shaped.face;
                if (faceId != kInvalidCollectionFace) {
                    rastFace = fontCollection->rasterizerFaceId(faceId);
                    glyphIdx = shaped.glyph_index;
                    fontHasGlyph = (glyphIdx != 0);
                }
            } else {
                // Single codepoint: use fast cmap lookup
                faceId = fontCollection->resolveFace(cell.codepoint);
                if (faceId != kInvalidCollectionFace) {
                    rastFace = fontCollection->rasterizerFaceId(faceId);
                    glyphIdx = rasterizer->getGlyphIndex(rastFace, cell.codepoint);
                    fontHasGlyph = (glyphIdx != 0);
                }
            }

            // Powerline fallback: if font doesn't have glyph, use procedural
            if (!fontHasGlyph && isPowerline) {
                GlyphKey boxKey{kInvalidFontFace, static_cast<uint32_t>(cp), {0, 0}};
                auto boxInfo = glyphCache->get(boxKey);
                if (!boxInfo) {
                    BoxGlyphBitmap boxBitmap = render_box_glyph(
                        cp, static_cast<int>(cellW), static_cast<int>(cellH));
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
                    D3DCellInstance inst = {};
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
                    inst.flags = 1;  // has_glyph
                    cellInstances.push_back(inst);
                }
                continue;
            }

            if (!fontHasGlyph) continue;

            GlyphKey key{rastFace, glyphIdx, {0, 0}};
            auto info = glyphCache->getOrRasterize(
                key, fontSize, *rasterizer, *glyphAtlas);
            if (!info || info->region.width <= 0 ||
                info->region.height <= 0) {
                continue;
            }

            D3DCellInstance inst = {};

            float offsetX, offsetY;
            if (isPowerline) {
                // Powerline glyphs fill the entire cell — anchor at cell origin
                offsetX = 0.0f;
                offsetY = 0.0f;
            } else {
                offsetX = static_cast<float>(info->region.bearing_x);
                offsetY = ascent -
                    static_cast<float>(info->region.bearing_y);
            }

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

            int sht2 = searchHighlightType(row, col);
            if (sht2 > 0) {
                inst.fg_color[0] = 0.0f; inst.fg_color[1] = 0.0f;
                inst.fg_color[2] = 0.0f; inst.fg_color[3] = 1.0f;
            }

            inst.flags = 1;  // has_glyph
            if (info->is_color) inst.flags |= 2;

            cellInstances.push_back(inst);
        }
    }

    // Pass 3: Underlines
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            // Use underline_style if set, else fall back to AttrUnderline attribute
            uint8_t ulStyle = cell.underline_style;
            if (ulStyle == 0 && (cell.attributes & AttrUnderline)) {
                ulStyle = 1; // default to single underline
            }
            if (ulStyle == 0) continue;

            // Determine underline color
            float ulColor[4];
            if (cell.underline_color != kColorDefault) {
                colorFromRGBA(colors.resolveFg(cell.underline_color), ulColor);
            } else {
                colorFromRGBA(colors.resolveFg(cell.fg_color), ulColor);
            }

            // Handle inverse attribute
            if (cell.attributes & AttrInverse) {
                // When inverse, fg and bg are swapped; underline should use the swapped fg
                colorFromRGBA(colors.resolveBg(cell.bg_color), ulColor);
                if (cell.underline_color != kColorDefault) {
                    colorFromRGBA(colors.resolveFg(cell.underline_color), ulColor);
                }
            }

            auto pushUnderlineRect = [&](float y, float h, uint32_t flags, uint32_t extraFlags) {
                D3DCellInstance inst = {};
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
                case 1: // single - 1px line at bottom
                    pushUnderlineRect(baseY + cellH - 2.0f, 1.0f, 8, 1);
                    break;
                case 2: // double - two 1px lines
                    pushUnderlineRect(baseY + cellH - 3.0f, 1.0f, 8, 2);
                    pushUnderlineRect(baseY + cellH - 1.0f, 1.0f, 8, 2);
                    break;
                case 3: // curly - 3px high, shader-patterned
                    pushUnderlineRect(baseY + cellH - 4.0f, 3.0f, 16, 3);
                    break;
                case 4: // dotted - 1px, shader-patterned
                    pushUnderlineRect(baseY + cellH - 2.0f, 1.0f, 16, 4);
                    break;
                case 5: // dashed - 1px, shader-patterned
                    pushUnderlineRect(baseY + cellH - 2.0f, 1.0f, 16, 5);
                    break;
                default:
                    break;
            }
        }
    }

    // Pass 3b: URL underlines (single underline in URL color)
    for (const auto& uh : urlHighlights) {
        float ulColor[4];
        colorFromRGBA(uh.color, ulColor);
        // Hovered URLs get brighter alpha
        if (uh.hovered) {
            ulColor[3] = 1.0f;
        } else {
            ulColor[3] = 0.6f;
        }
        for (int col = uh.startCol; col < uh.endCol && col < cols; ++col) {
            // Skip if cell already has a terminal underline
            const TermCell& cell = screen.cellAt(uh.row, col);
            if (cell.underline_style != 0 || (cell.attributes & AttrUnderline)) continue;

            float baseY = uh.row * cellH + gridOffsetY;
            D3DCellInstance inst = {};
            inst.position[0] = col * cellW;
            inst.position[1] = baseY + cellH - 2.0f;
            inst.atlas_size[0] = cellW;
            inst.atlas_size[1] = 1.0f;
            inst.bg_color[0] = ulColor[0];
            inst.bg_color[1] = ulColor[1];
            inst.bg_color[2] = ulColor[2];
            inst.bg_color[3] = ulColor[3];
            inst.flags = 8;       // is_underline (bit3 for underline rect rendering)
            inst.extra_flags = 1; // single underline style
            cellInstances.push_back(inst);
        }
    }

    // Record insertion point before cursor instances
    cellCountBeforeCursor = cellInstances.size();

    // Pass 4: Cursor
    appendCursorInstances(screen, cellW, cellH, gridOffsetY);

    // Passes 5-9: Overlay elements (status bar, scrollbar, resize overlay,
    // tab bar with notification indicators, pane borders with glow).
    buildOverlayPasses(screen, cellW, cellH, ascent, fontSize);
}

void D3DTextRenderer::Impl::appendCursorInstances(
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
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH + gridOffsetY;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.bg_color[3] = 0.5f;
                inst.flags = 4;  // is_bg_pass
                cellInstances.push_back(inst);
            } else if (shape == CursorShape::Bar) {
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH + gridOffsetY;
                inst.atlas_size[0] = 2.0f;
                inst.atlas_size[1] = cellH;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.flags = 8;  // is_cursor
                cellInstances.push_back(inst);
            } else if (shape == CursorShape::Underline) {
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH + gridOffsetY + cellH - 2.0f;
                inst.atlas_size[0] = cellW;
                inst.atlas_size[1] = 2.0f;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.flags = 8;  // is_cursor
                cellInstances.push_back(inst);
            }
        }
    }
}

void D3DTextRenderer::Impl::patchCursorOnly(const Screen& screen) {
    if (!fontCollection) return;

    FontMetrics metrics = fontCollection->primaryMetrics();
    float cellW = metrics.cell_width;
    float cellH = metrics.cell_height;

    float tabBarH = cellH * D3DTextRenderer::kTabBarHeightScale;
    float gridOffsetY = (tabBar.visible && !tabBar.tabs.empty()) ? tabBarH : 0.0f;

    // Remove old cursor instances (everything from cursor onward, before overlays)
    cellInstances.resize(cellCountBeforeCursor);

    // Append fresh cursor instances
    appendCursorInstances(screen, cellW, cellH, gridOffsetY);

    // Re-append overlay passes
    float ascent = metrics.ascent;
    float fontSize = fontCollection->fontSize();
    buildOverlayPasses(screen, cellW, cellH, ascent, fontSize);
}

} // namespace termcore

#endif // _WIN32
