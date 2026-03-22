#import "MetalTextRendererImpl.h"
#import <algorithm>
#include "termcore/font/box_drawing.h"

namespace termcore {

// ---------------------------------------------------------------------------
// buildCellBuffer -- clean 2-pass (backgrounds then glyphs)
// ---------------------------------------------------------------------------
void MetalTextRenderer::Impl::buildCellBuffer(const Screen& screen) {
    if (!fontCollection || !glyphCache || !glyphAtlas || !rasterizer)
        return;

    FontMetrics metrics = fontCollection->primaryMetrics();
    float cellW = metrics.cell_width;
    float cellH = metrics.cell_height;
    float ascent = metrics.ascent;
    float fontSize = fontCollection->fontSize();
    int rows = screen.rows();
    int cols = screen.cols();

    cellInstances.clear();
    cellInstances.reserve(static_cast<size_t>(rows) * cols * 2);

    const auto& dc = screen.dynamicColors();

    uint8_t bgAlpha = static_cast<uint8_t>(255.0f * backgroundOpacity);

    // Selection highlight color (use highlight_bg from dynamic colors, with fg as contrast)
    uint32_t selBg = dc.highlight_bg;
    uint32_t selFg = dc.background; // dark text on highlight

    // Pass 1: Background quads
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            uint32_t fg = dc.resolveFg(cell.fg_color);
            uint32_t bg = dc.resolveBg(cell.bg_color);
            if (cell.attributes & AttrInverse) std::swap(fg, bg);

            // Minimum contrast adjustment
            if (minimumContrast > 1.0f) {
                fg = ensureContrast(fg, bg, minimumContrast);
            }

            // Apply selection highlight: swap fg/bg for selected cells
            bool selected = selection.contains(row, col);
            if (selected) {
                bg = selBg;
                fg = selFg;
            }

            CellInstance inst = {};
            inst.grid_col = static_cast<uint16_t>(col);
            inst.grid_row = static_cast<uint16_t>(row);
            inst.fg_r = (fg >> 16) & 0xFF;
            inst.fg_g = (fg >> 8) & 0xFF;
            inst.fg_b = fg & 0xFF;
            inst.fg_a = 255;
            inst.bg_r = (bg >> 16) & 0xFF;
            inst.bg_g = (bg >> 8) & 0xFF;
            inst.bg_b = bg & 0xFF;
            inst.bg_a = bgAlpha;
            inst.flags = 4; // bg pass
            cellInstances.push_back(inst);
        }
    }

    // Pass 2: Glyph quads
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);
            if (cell.codepoint <= ' ') continue;

            // Skip continuation cells (second cell of a wide character,
            // marked with width=0 and codepoint=0)
            if (cell.width == 0) continue;

            char32_t cp = cell.codepoint;

            // Check for procedural box drawing
            bool is_box_drawing =
                (cp >= 0x2500 && cp <= 0x259F) ||
                (cp >= 0x2800 && cp <= 0x28FF) ||
                (cp >= 0xE0B0 && cp <= 0xE0B3);

            if (is_box_drawing) {
                // Use a special GlyphKey with kInvalidFontFace to distinguish from font glyphs
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
                    uint32_t fg = dc.resolveFg(cell.fg_color);
                    uint32_t bg = dc.resolveBg(cell.bg_color);
                    if (cell.attributes & AttrInverse) std::swap(fg, bg);
                    if (minimumContrast > 1.0f) {
                        fg = ensureContrast(fg, bg, minimumContrast);
                    }
                    if (selection.contains(row, col)) {
                        fg = selFg; bg = selBg;
                    }

                    CellInstance inst = {};
                    inst.grid_col = static_cast<uint16_t>(col);
                    inst.grid_row = static_cast<uint16_t>(row);
                    inst.glyph_x = static_cast<uint16_t>(boxInfo->region.x);
                    inst.glyph_y = static_cast<uint16_t>(boxInfo->region.y);
                    inst.glyph_width = static_cast<uint16_t>(boxInfo->region.width);
                    inst.glyph_height = static_cast<uint16_t>(boxInfo->region.height);
                    // Box drawing fills cell from top-left
                    inst.offset_x = 0;
                    inst.offset_y = 0;
                    inst.fg_r = (fg >> 16) & 0xFF;
                    inst.fg_g = (fg >> 8) & 0xFF;
                    inst.fg_b = fg & 0xFF;
                    inst.fg_a = 255;
                    inst.bg_r = (bg >> 16) & 0xFF;
                    inst.bg_g = (bg >> 8) & 0xFF;
                    inst.bg_b = bg & 0xFF;
                    inst.bg_a = 255;
                    inst.flags = 1; // has_glyph
                    cellInstances.push_back(inst);
                }
                continue;  // Skip normal font rendering for this cell
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
            if (!info) continue;

            uint32_t fg = dc.resolveFg(cell.fg_color);
            uint32_t bg = dc.resolveBg(cell.bg_color);
            if (cell.attributes & AttrInverse) std::swap(fg, bg);
            if (minimumContrast > 1.0f) {
                fg = ensureContrast(fg, bg, minimumContrast);
            }
            if (selection.contains(row, col)) {
                fg = selFg; bg = selBg;
            }

            bool is_wide = (cell.width == 2);

            CellInstance inst = {};
            inst.grid_col = static_cast<uint16_t>(col);
            inst.grid_row = static_cast<uint16_t>(row);
            inst.glyph_x =
                static_cast<uint16_t>(info->region.x);
            inst.glyph_y =
                static_cast<uint16_t>(info->region.y);
            inst.glyph_width =
                static_cast<uint16_t>(info->region.width);
            inst.glyph_height =
                static_cast<uint16_t>(info->region.height);

            // offset_y = ascent - bearing_y (distance from cell top
            // to glyph top)
            inst.offset_y = static_cast<int16_t>(
                static_cast<int>(ascent) - info->region.bearing_y);

            if (is_wide) {
                // Center glyph horizontally across the 2-cell span
                float spanWidth = cellW * 2.0f;
                if (info->region.width < static_cast<int>(spanWidth)) {
                    int16_t center_offset = static_cast<int16_t>(
                        (spanWidth - info->region.width) / 2.0f);
                    inst.offset_x = static_cast<int16_t>(
                        center_offset + info->region.bearing_x);
                } else {
                    inst.offset_x =
                        static_cast<int16_t>(info->region.bearing_x);
                }
            } else {
                // offset_x = bearing_x (signed, from cell left edge)
                inst.offset_x =
                    static_cast<int16_t>(info->region.bearing_x);
            }

            inst.fg_r = (fg >> 16) & 0xFF;
            inst.fg_g = (fg >> 8) & 0xFF;
            inst.fg_b = fg & 0xFF;
            inst.fg_a = 255;
            inst.bg_r = (bg >> 16) & 0xFF;
            inst.bg_g = (bg >> 8) & 0xFF;
            inst.bg_b = bg & 0xFF;
            inst.bg_a = 255;

            inst.flags = 1; // has_glyph
            if (info->is_color) inst.flags |= 2;
            cellInstances.push_back(inst);
        }
    }

    // --- Pass 3: Underline for cells with AttrUnderline ---
    // (Moved before cursor so cursor is always last)
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);
            if (!(cell.attributes & AttrUnderline)) continue;

            uint32_t fg = dc.resolveFg(cell.fg_color);
            CellInstance ulInst = {};
            ulInst.grid_col = static_cast<uint16_t>(col);
            ulInst.grid_row = static_cast<uint16_t>(row);
            ulInst.flags = 4; // bg pass (solid rect)
            // Position underline at bottom of cell, 2px thick
            ulInst.offset_y = static_cast<int16_t>(cellH - 2);
            ulInst.glyph_width = static_cast<uint16_t>(cellW);
            ulInst.glyph_height = 2;
            ulInst.bg_r = (fg >> 16) & 0xFF;
            ulInst.bg_g = (fg >> 8) & 0xFF;
            ulInst.bg_b = fg & 0xFF;
            ulInst.bg_a = 255;
            cellInstances.push_back(ulInst);
        }
    }

    // --- Pass 3b: URL highlight underline (Cmd+hover) ---
    if (urlHighlightRow >= 0 && urlHighlightRow < rows &&
        urlHighlightStartCol >= 0 && urlHighlightEndCol > urlHighlightStartCol) {
        int endC = std::min(urlHighlightEndCol, cols);
        for (int col = urlHighlightStartCol; col < endC; ++col) {
            const TermCell& cell = screen.cellAt(urlHighlightRow, col);
            uint32_t fg = dc.resolveFg(cell.fg_color);
            CellInstance ulInst = {};
            ulInst.grid_col = static_cast<uint16_t>(col);
            ulInst.grid_row = static_cast<uint16_t>(urlHighlightRow);
            ulInst.flags = 4; // bg pass (solid rect)
            ulInst.offset_y = static_cast<int16_t>(cellH - 2);
            ulInst.glyph_width = static_cast<uint16_t>(cellW);
            ulInst.glyph_height = 2;
            ulInst.bg_r = (fg >> 16) & 0xFF;
            ulInst.bg_g = (fg >> 8) & 0xFF;
            ulInst.bg_b = fg & 0xFF;
            ulInst.bg_a = 255;
            cellInstances.push_back(ulInst);
        }
    }

    // Record insertion point before cursor instances
    cellCountBeforeCursor = cellInstances.size();

    // --- Pass 4: Cursor ---
    appendCursorInstances(screen, cellW, cellH);
}

// -----------------------------------------------------------------
// Append cursor instances (can be called independently for blink)
// -----------------------------------------------------------------
void MetalTextRenderer::Impl::appendCursorInstances(const Screen& screen,
                                                      float cellW, float cellH) {
    // Update blink state (time-based)
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    if (now - lastBlinkToggle >= blinkInterval) {
        cursorBlinkOn = !cursorBlinkOn;
        lastBlinkToggle = now;
    }

    int rows = screen.rows();
    int cols = screen.cols();
    bool showCursor = screen.cursorVisible() && cursorBlinkOn && !imeActive;
    if (showCursor) {
        int cRow = screen.cursorRow();
        int cCol = screen.cursorCol();
        if (cRow >= 0 && cRow < rows && cCol >= 0 && cCol < cols) {
            const auto& dc = screen.dynamicColors();
            uint32_t cursorColor = dc.resolveFg(dc.cursor_color);

            CellInstance cursorInst = {};
            cursorInst.grid_col = static_cast<uint16_t>(cCol);
            cursorInst.grid_row = static_cast<uint16_t>(cRow);
            cursorInst.flags = 4; // is_bg_pass
            cursorInst.bg_r = (cursorColor >> 16) & 0xFF;
            cursorInst.bg_g = (cursorColor >> 8) & 0xFF;
            cursorInst.bg_b = cursorColor & 0xFF;
            cursorInst.bg_a = 255;

            CursorShape shape = screen.cursorShape();
            if (shape == CursorShape::Bar) {
                cursorInst.glyph_width = 2;
                cursorInst.glyph_height = static_cast<uint16_t>(cellH);
            } else if (shape == CursorShape::Underline) {
                cursorInst.offset_y = static_cast<int16_t>(cellH - 2);
                cursorInst.glyph_height = 2;
                cursorInst.glyph_width = static_cast<uint16_t>(cellW);
            }
            // Block cursor: default flags=4 renders full cell bg

            cellInstances.push_back(cursorInst);
        }
    }
}

// -----------------------------------------------------------------
// Patch cursor in-place without full rebuild (for blink toggling)
// -----------------------------------------------------------------
void MetalTextRenderer::Impl::patchCursorOnly(const Screen& screen) {
    if (!fontCollection) return;
    FontMetrics metrics = fontCollection->primaryMetrics();
    float cellW = metrics.cell_width;
    float cellH = metrics.cell_height;

    // Remove old cursor instances (everything after cellCountBeforeCursor)
    cellInstances.resize(cellCountBeforeCursor);
    // Append fresh cursor instances
    appendCursorInstances(screen, cellW, cellH);
}

} // namespace termcore
