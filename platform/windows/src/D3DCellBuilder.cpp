#if defined(_WIN32)

#include "D3DTextRendererImpl.h"

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

    // Pass 1: Background quads (cell-sized)
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            D3DCellInstance inst = {};
            inst.position[0] = col * cellW;
            inst.position[1] = row * cellH;

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

            D3DCellInstance inst = {};

            float offsetX = static_cast<float>(info->region.bearing_x);
            float offsetY = ascent -
                static_cast<float>(info->region.bearing_y);

            inst.position[0] = col * cellW + offsetX;
            inst.position[1] = row * cellH + offsetY;

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

    // Pass 3: Cursor
    if (screen.cursorVisible() && cursorBlinkVisible) {
        int cRow = screen.cursorRow();
        int cCol = screen.cursorCol();
        if (cRow >= 0 && cRow < rows && cCol >= 0 && cCol < cols) {
            uint32_t cursorColor =
                colors.resolveFg(colors.cursor_color);

            CursorShape shape = screen.cursorShape();

            if (shape == CursorShape::Block) {
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.bg_color[3] = 0.5f;
                inst.flags = 4;  // is_bg_pass
                cellInstances.push_back(inst);
            } else if (shape == CursorShape::Bar) {
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH;
                inst.atlas_size[0] = 2.0f;
                inst.atlas_size[1] = cellH;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.flags = 8;  // is_cursor
                cellInstances.push_back(inst);
            } else if (shape == CursorShape::Underline) {
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW;
                inst.position[1] = cRow * cellH + cellH - 2.0f;
                inst.atlas_size[0] = cellW;
                inst.atlas_size[1] = 2.0f;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.flags = 8;  // is_cursor
                cellInstances.push_back(inst);
            }
        }
    }

    // Pass 4: Underlines
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

            float baseY = row * cellH;

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
}

} // namespace termcore

#endif // _WIN32
