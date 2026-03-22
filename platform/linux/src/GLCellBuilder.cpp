#include "GLTextRendererImpl.h"

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

int GLTextRenderer::Impl::searchHighlightType(int row, int col) const {
    for (int i = 0; i < static_cast<int>(searchHighlights.size()); ++i) {
        const auto& h = searchHighlights[i];
        if (h.row == row && col >= h.startCol && col < h.endCol) {
            return (i == searchCurrentIndex) ? 2 : 1;
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

            inst.flags = 4;  // is_bg_pass
            inst.extra_flags = 0;
            cellInstances.push_back(inst);
        }
    }

    // Pass 2: Glyph quads (glyph-sized, positioned with bearing)
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            if (cell.codepoint == ' ' || cell.codepoint == 0) {
                continue;
            }

            CollectionFaceId faceId =
                fontCollection->resolveFace(cell.codepoint);
            if (faceId == kInvalidCollectionFace) continue;

            FontFaceId rastFace =
                fontCollection->rasterizerFaceId(faceId);
            uint32_t glyphIdx =
                rasterizer->getGlyphIndex(rastFace, cell.codepoint);
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

    if (screen.cursorVisible() && cursorBlinkVisible) {
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
