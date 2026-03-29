#if defined(_WIN32)

#include "D3DTextRendererImpl.h"
#include "ScreenSnapshot.h"
#include "termcore/font/box_drawing.h"
#include "termcore/font/unicode_width.h"
#include "termcore/kitty_unicode_placeholder.h"
#include <cmath>
#include <cstring>

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

void D3DTextRenderer::Impl::rebuildSearchIndex() {
    searchByRow.clear();
    for (int i = 0; i < static_cast<int>(searchHighlights.size()); ++i) {
        searchByRow[searchHighlights[i].row].push_back({i, i});
    }
}

void D3DTextRenderer::Impl::rebuildUrlIndex() {
    urlByRow.clear();
    for (size_t i = 0; i < urlHighlights.size(); ++i) {
        urlByRow[urlHighlights[i].row].push_back(i);
    }
}

int D3DTextRenderer::Impl::searchHighlightType(int row, int col) const {
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

const D3DTextRenderer::UrlHighlight* D3DTextRenderer::Impl::urlHighlightAt(int row, int col) const {
    auto it = urlByRow.find(row);
    if (it == urlByRow.end()) return nullptr;
    for (size_t idx : it->second) {
        const auto& h = urlHighlights[idx];
        if (col >= h.startCol && col < h.endCol) {
            return &h;
        }
    }
    return nullptr;
}

// --- Row-level HarfBuzz shaping ---

struct RowShapedGlyph {
    int col;                    // which cell this glyph belongs to
    uint32_t glyph_index;      // HarfBuzz glyph index
    FontFaceId shaper_face_id; // font face used for shaping
    FontFaceId raster_face_id; // font face used for rasterization
    int x_offset_26_6;         // sub-pixel offset from advance (26.6 fixed point)
    int cell_span;             // how many cells this glyph covers (for ligatures)
};

template<typename ScreenT>
static std::vector<RowShapedGlyph> shapeRow(
    const ScreenT& screen, int row, int cols,
    FontCollection* fontCollection,
    const ShaperConfig& config,
    int cursorRow, int cursorCol)
{
    std::vector<RowShapedGlyph> result;
    if (!fontCollection) return result;

    // 1. Collect codepoints and resolve font faces
    struct CellInfo {
        char32_t codepoint;
        CollectionFaceId collection_face;
    };
    std::vector<CellInfo> cells(cols);

    for (int c = 0; c < cols; ++c) {
        const TermCell& tc = screen.cellAt(row, c);
        char32_t cp = tc.codepoint;
        if (cp == 0 || tc.width == 0) {
            cells[c] = {' ', fontCollection->resolveFace(' ')};
        } else {
            cells[c] = {cp, fontCollection->resolveFace(cp)};
        }
    }

    // 2. Group into runs by font face
    int run_start = 0;
    while (run_start < cols) {
        CollectionFaceId face = cells[run_start].collection_face;
        int run_end = run_start + 1;
        while (run_end < cols && cells[run_end].collection_face == face) {
            ++run_end;
        }

        // 3. Build codepoint string for this run
        std::u32string run_codepoints;
        run_codepoints.reserve(run_end - run_start);
        for (int c = run_start; c < run_end; ++c) {
            run_codepoints.push_back(cells[c].codepoint);
        }

        // 4. Get shaper/raster face IDs
        FontFaceId shaperFace = (face != kInvalidCollectionFace)
            ? fontCollection->shaperFaceId(face) : kInvalidFontFace;
        FontFaceId rasterFace = (face != kInvalidCollectionFace)
            ? fontCollection->rasterizerFaceId(face) : kInvalidFontFace;

        // 5. Shape the run
        if (shaperFace != kInvalidFontFace) {
            FontMetrics metrics = fontCollection->primaryMetrics();
            float cellWidth = metrics.cell_width;

            // If cursor is in this run, split into sub-runs around cursor
            // to break ligatures at cursor position
            bool cursorInRun = (row == cursorRow &&
                               cursorCol >= run_start && cursorCol < run_end);

            // Helper: emit glyphs from shaped runs into result.
            // Each ShapedRun may contain multiple glyphs for contiguous cells.
            // - Ligature: glyphs.size() < cell_count → first glyph covers all cells
            // - Normal: glyphs.size() == cell_count → one glyph per cell
            auto emitRuns = [&](const std::vector<ShapedRun>& shaped_runs,
                                int colOffset) {
                for (const auto& sr : shaped_runs) {
                    if (sr.glyphs.empty()) continue;

                    bool isLigature = (sr.glyphs.size() < static_cast<size_t>(sr.cell_count));
                    if (isLigature) {
                        // Ligature: single composite glyph spanning multiple cells
                        const auto& g = sr.glyphs[0];
                        RowShapedGlyph rsg;
                        rsg.col = colOffset + sr.start_cell;
                        rsg.glyph_index = g.glyph_index;
                        rsg.shaper_face_id = shaperFace;
                        rsg.raster_face_id = rasterFace;
                        rsg.x_offset_26_6 = g.x_offset;
                        rsg.cell_span = sr.cell_count;
                        result.push_back(rsg);
                    } else {
                        // Normal: each glyph maps to its own cell(s)
                        int cellIdx = sr.start_cell;
                        for (size_t gi = 0; gi < sr.glyphs.size(); ++gi) {
                            const auto& g = sr.glyphs[gi];
                            // Determine this glyph's cell span from cluster info
                            int nextCell;
                            if (gi + 1 < sr.glyphs.size()) {
                                uint32_t nextCluster = sr.glyphs[gi + 1].cluster;
                                uint32_t thisCluster = g.cluster;
                                nextCell = cellIdx + static_cast<int>(nextCluster - thisCluster);
                                if (nextCell <= cellIdx) nextCell = cellIdx + 1;
                            } else {
                                nextCell = sr.start_cell + sr.cell_count;
                            }
                            int span = nextCell - cellIdx;
                            if (span < 1) span = 1;

                            RowShapedGlyph rsg;
                            rsg.col = colOffset + cellIdx;
                            rsg.glyph_index = g.glyph_index;
                            rsg.shaper_face_id = shaperFace;
                            rsg.raster_face_id = rasterFace;
                            rsg.x_offset_26_6 = g.x_offset;
                            rsg.cell_span = span;
                            result.push_back(rsg);

                            cellIdx = nextCell;
                        }
                    }
                }
            };

            if (cursorInRun) {
                // Shape three segments: before cursor, cursor cell, after cursor
                int segments[][2] = {
                    {run_start, cursorCol},
                    {cursorCol, cursorCol + 1},
                    {cursorCol + 1, run_end}
                };
                for (auto& seg : segments) {
                    int segStart = seg[0];
                    int segEnd = seg[1];
                    if (segStart >= segEnd) continue;

                    std::u32string segCp;
                    segCp.reserve(segEnd - segStart);
                    for (int c = segStart; c < segEnd; ++c) {
                        segCp.push_back(cells[c].codepoint);
                    }

                    auto shaped_runs = fontCollection->shaper().shapeForGrid(
                        shaperFace, segCp, cellWidth, config);
                    emitRuns(shaped_runs, segStart);
                }
            } else {
                auto shaped_runs = fontCollection->shaper().shapeForGrid(
                    shaperFace, run_codepoints, cellWidth, config);
                emitRuns(shaped_runs, run_start);
            }
        } else {
            // Fallback: no shaping, just individual glyphs
            for (int c = run_start; c < run_end; ++c) {
                if (cells[c].codepoint == ' ' || cells[c].codepoint == 0) continue;
                RowShapedGlyph rsg;
                rsg.col = c;
                rsg.glyph_index = 0; // will be resolved individually
                rsg.shaper_face_id = kInvalidFontFace;
                rsg.raster_face_id = rasterFace;
                rsg.x_offset_26_6 = 0;
                rsg.cell_span = 1;
                result.push_back(rsg);
            }
        }

        run_start = run_end;
    }

    return result;
}

template<typename ScreenT>
void D3DTextRenderer::Impl::buildCellBuffer(const ScreenT& screen) {
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
    float tabBarH = cellH * tabBar.height_scale;
    float gridOffsetY = (tabBar.visible && !tabBar.tabs.empty()) ? tabBarH : 0.0f;

    // Offset grid right when sidebar is visible
    float gridOffsetX = sidebar.visible ? static_cast<float>(sidebar.width) : 0.0f;

    // Invalidate row caches when grid dimensions change
    if (rows != cachedRows || cols != cachedCols) {
        rowCaches.clear();
        rowCaches.resize(rows);
        cachedRows = rows;
        cachedCols = cols;
    }

    // When non-screen state changes (selection, search highlights, opacity, etc.),
    // contentDirty is set but screen rows may not be dirty. In that case, invalidate
    // all row caches so they get rebuilt with the new visual state.
    bool useRowCache = screen.isDirty();
    if (!useRowCache) {
        for (auto& rc : rowCaches) rc.valid = false;
    }

    // Pass 1: Background quads (cell-sized)
    for (int row = 0; row < rows; ++row) {
        // Dirty row optimization: reuse cached bg instances for clean rows
        if (useRowCache && !screen.isRowDirty(row)
            && row < static_cast<int>(rowCaches.size())
            && rowCaches[row].valid) {
            for (const auto& inst : rowCaches[row].bgInstances)
                cellInstances.push_back(inst);
            continue;
        }

        size_t bgStartIdx = cellInstances.size();
        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);


            D3DCellInstance inst = {};
            inst.position[0] = col * cellW + gridOffsetX;
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

        // Cache this row's bg instances
        if (row < static_cast<int>(rowCaches.size())) {
            rowCaches[row].bgInstances.assign(
                cellInstances.begin() + bgStartIdx, cellInstances.end());
        }
    }

    // Pass 1b: Margin quads — fill gaps between cell grid and viewport edges
    // so margins have the same opacity as the cell background.
    {
        float gridRight  = cols * cellW + gridOffsetX;
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
            D3DCellInstance inst = {};
            inst.position[0] = gridRight;
            inst.position[1] = gridOffsetY;
            inst.atlas_size[0] = viewportWidth - gridRight;
            inst.atlas_size[1] = viewportHeight - gridOffsetY;
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
            inst.position[0] = gridOffsetX;
            inst.position[1] = gridBottom;
            inst.atlas_size[0] = gridRight - gridOffsetX;  // only up to grid width (right margin covers rest)
            inst.atlas_size[1] = viewportHeight - gridBottom;
            inst.bg_color[0] = defaultBg[0];
            inst.bg_color[1] = defaultBg[1];
            inst.bg_color[2] = defaultBg[2];
            inst.bg_color[3] = defaultBg[3];
            inst.flags = 8;  // render as rect
            cellInstances.push_back(inst);
        }

        // Top margin (tab bar area): fill with same bg+opacity as terminal cells
        // so acrylic blur sees identical alpha across the entire window.
        if (gridOffsetY > 0.0f) {
            D3DCellInstance inst = {};
            inst.position[0] = 0.0f;
            inst.position[1] = 0.0f;
            inst.atlas_size[0] = viewportWidth;
            inst.atlas_size[1] = gridOffsetY;
            inst.bg_color[0] = defaultBg[0];
            inst.bg_color[1] = defaultBg[1];
            inst.bg_color[2] = defaultBg[2];
            inst.bg_color[3] = defaultBg[3];
            inst.flags = 8;
            cellInstances.push_back(inst);
        }
    }

    // Cursor position for ligature breaking
    int cursorRow = screen.cursorRow();
    int cursorCol = screen.cursorCol();

    // Pass 2: Glyph quads (glyph-sized, positioned with bearing)
    for (int row = 0; row < rows; ++row) {

        // Dirty row optimization: reuse cached fg instances for clean rows
        if (useRowCache && !screen.isRowDirty(row)
            && row < static_cast<int>(rowCaches.size())
            && rowCaches[row].valid) {
            for (const auto& inst : rowCaches[row].fgInstances)
                cellInstances.push_back(inst);
            continue;
        }

        size_t fgStartIdx = cellInstances.size();

        // --- Row-level shaping (replaces per-cell ligature detection) ---
        // When fontCollection is available, shape entire font runs per row
        // using shapeForGrid() for improved text layout and ligature support.
        auto shapedRow = shapeRow(screen, row, cols, fontCollection,
                                  ShaperConfig{fontLigatures, fontLigatures},
                                  cursorRow, cursorCol);

        // Build a map from col -> shaped glyph info
        std::vector<const RowShapedGlyph*> colToShaped(cols, nullptr);
        std::vector<bool> colIsContinuation(cols, false);

        for (const auto& sg : shapedRow) {
            if (sg.col >= 0 && sg.col < cols) {
                colToShaped[sg.col] = &sg;
                // Mark continuation cells for multi-cell shaped glyphs
                for (int c = sg.col + 1; c < sg.col + sg.cell_span && c < cols; ++c) {
                    colIsContinuation[c] = true;
                }
            }
        }

        for (int col = 0; col < cols; ++col) {
            const TermCell& cell = screen.cellAt(row, col);

            // Kitty Unicode Placeholder: skip glyph rendering for image cells.
            if (cell.codepoint == kKittyPlaceholder) {
                continue;
            }

            if (cell.codepoint == ' ' || cell.codepoint == 0) {
                continue;
            }
            if (cell.width == 0) continue;  // Skip continuation cells

            char32_t cp = cell.codepoint;

            // Powerline glyphs: try font first, procedural fallback
            bool isPowerline = is_powerline_extended(cp);
            bool isNerdIcon = is_nerd_font_icon(cp);
            // Non-powerline box drawing: always procedural
            bool isBoxDrawing = !isPowerline && is_box_drawing(cp);

            // --- Handle row-shaped glyph cells ---
            // Check AFTER special character detection so box drawing, Powerline,
            // and placeholder glyphs are handled by their dedicated paths.
            if (!isBoxDrawing && !isPowerline && colToShaped[col]) {
                const auto& sg = *colToShaped[col];
                if (sg.glyph_index != 0 && sg.raster_face_id != kInvalidFontFace) {
                    GlyphKey key{sg.raster_face_id, sg.glyph_index, {0, 0}};
                    auto info = glyphCache->getOrRasterize(
                        key, fontSize, *rasterizer, *glyphAtlas);
                    if (info && info->region.width > 0 && info->region.height > 0) {
                        D3DCellInstance inst = {};

                        float offsetX = static_cast<float>(info->region.bearing_x);
                        float offsetY = ascent -
                            static_cast<float>(info->region.bearing_y);

                        // Apply sub-pixel offset from shaping (26.6 fixed point -> pixels)
                        float shapingOffsetX = static_cast<float>(sg.x_offset_26_6) / 64.0f;

                        inst.position[0] = col * cellW + offsetX + shapingOffsetX + gridOffsetX;
                        inst.position[1] = row * cellH + offsetY + gridOffsetY;

                        inst.atlas_uv[0] = static_cast<float>(info->region.x);
                        inst.atlas_uv[1] = static_cast<float>(info->region.y);
                        inst.atlas_size[0] = static_cast<float>(info->region.width);
                        inst.atlas_size[1] = static_cast<float>(info->region.height);

                        // Nerd Font icon cell constraint for shaped glyphs
                        if (isNerdIcon) {
                            float maxW = cellW * 2.0f;
                            float maxH = cellH;
                            float glyphW = inst.atlas_size[0];
                            float glyphH = inst.atlas_size[1];
                            if (glyphW > maxW || glyphH > maxH) {
                                float scale = (std::min)(maxW / glyphW, maxH / glyphH);
                                float scaledW = glyphW * scale;
                                float scaledH = glyphH * scale;
                                inst.atlas_size[0] = scaledW;
                                inst.atlas_size[1] = scaledH;
                                inst.position[0] = col * cellW + (cellW - scaledW) / 2.0f + gridOffsetX;
                                inst.position[1] = row * cellH + (cellH - scaledH) / 2.0f + gridOffsetY;
                            }
                        }

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

                        cellInstances.push_back(inst);
                        continue;
                    }
                }
                // Fall through to per-cell resolution if shaped glyph was 0 or rasterization failed
            } else if (!isBoxDrawing && !isPowerline && colIsContinuation[col]) {
                continue;  // Skip continuation cells of shaped multi-cell runs
            }

            if (isBoxDrawing) {
                // GPU path for box drawing (U+2500-257F) and block elements (U+2580-259F)
                bool isGpuBoxDraw = (cp >= 0x2500 && cp <= 0x257F);
                bool isGpuBlock   = (cp >= 0x2580 && cp <= 0x259F);

                if (isGpuBoxDraw || isGpuBlock) {
                    // Render in pixel shader — no atlas needed
                    uint32_t render_mode = isGpuBlock ? 2u : 1u;
                    D3DCellInstance inst = {};
                    inst.position[0] = col * cellW + gridOffsetX;
                    inst.position[1] = row * cellH + gridOffsetY;

                    // Pass codepoint as float bits via atlas_uv[0]
                    // (VS forwards this to corner_radius for PS to read)
                    uint32_t cpVal = static_cast<uint32_t>(cp);
                    float cpAsFloat;
                    memcpy(&cpAsFloat, &cpVal, sizeof(float));
                    inst.atlas_uv[0] = cpAsFloat;
                    inst.atlas_uv[1] = 0.0f;

                    // Cell dimensions as quad size
                    inst.atlas_size[0] = cellW;
                    inst.atlas_size[1] = cellH;

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
                    // Set render_mode in bits 3-4 of extra_flags
                    inst.extra_flags = (render_mode << 3);
                    cellInstances.push_back(inst);
                    continue;
                }

                // CPU fallback path for Braille and other non-GPU box drawing chars
                int bitmapW = static_cast<int>(std::ceil(cellW));
                int bitmapH = static_cast<int>(std::ceil(cellH));
                GlyphKey boxKey{kInvalidFontFace, static_cast<uint32_t>(cp), {0, 0}};
                auto boxInfo = glyphCache->get(boxKey);
                if (!boxInfo) {
                    BoxGlyphBitmap boxBitmap = render_box_glyph(
                        cp, bitmapW, bitmapH);
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
                    inst.position[0] = col * cellW + gridOffsetX;
                    inst.position[1] = row * cellH + gridOffsetY;
                    inst.atlas_uv[0] = static_cast<float>(boxInfo->region.x);
                    inst.atlas_uv[1] = static_cast<float>(boxInfo->region.y);
                    inst.atlas_size[0] = cellW;
                    inst.atlas_size[1] = cellH;

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
                    inst.position[0] = col * cellW + gridOffsetX;
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

            inst.position[0] = col * cellW + offsetX + gridOffsetX;
            inst.position[1] = row * cellH + offsetY + gridOffsetY;

            inst.atlas_uv[0] = static_cast<float>(info->region.x);
            inst.atlas_uv[1] = static_cast<float>(info->region.y);
            inst.atlas_size[0] = static_cast<float>(info->region.width);
            inst.atlas_size[1] = static_cast<float>(info->region.height);

            // Cell constraint for Nerd Font icons: scale oversized glyphs to fit cell bounds
            if (isNerdIcon) {
                float maxW = cellW * 2.0f;
                float maxH = cellH;
                float glyphW = inst.atlas_size[0];
                float glyphH = inst.atlas_size[1];

                if (glyphW > maxW || glyphH > maxH) {
                    float scale = (std::min)(maxW / glyphW, maxH / glyphH);
                    float scaledW = glyphW * scale;
                    float scaledH = glyphH * scale;
                    // Adjust displayed size while keeping atlas UV the same
                    inst.atlas_size[0] = scaledW;
                    inst.atlas_size[1] = scaledH;
                    // Re-center in cell
                    inst.position[0] = col * cellW + (cellW - scaledW) / 2.0f + gridOffsetX;
                    inst.position[1] = row * cellH + (cellH - scaledH) / 2.0f + gridOffsetY;
                }
            }

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

            cellInstances.push_back(inst);
        }

        // Cache this row's fg instances and mark cache valid
        if (row < static_cast<int>(rowCaches.size())) {
            rowCaches[row].fgInstances.assign(
                cellInstances.begin() + fgStartIdx, cellInstances.end());
            rowCaches[row].valid = true;
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

            D3DCellInstance inst = {};
            float offsetX = static_cast<float>(info->region.bearing_x);
            float offsetY = ascent - static_cast<float>(info->region.bearing_y);

            inst.position[0] = gtCol * cellW + offsetX + gridOffsetX;
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
            cellInstances.push_back(inst);
            ++gtCol;
        }
    }

    // Pass 2c: IME composition overlay (virtual — does NOT mutate Screen cells)
    // Renders IME preedit text at cursor position by drawing bg + glyph quads
    // on top of existing content, similar to ghost text.
    if (!imeOverlay.text.empty() && imeOverlay.row >= 0 && imeOverlay.col >= 0 &&
        imeOverlay.row < rows) {
        int imeCol = imeOverlay.col;
        int imeRow = imeOverlay.row;

        for (size_t i = 0; i < imeOverlay.text.size() && imeCol < cols; ++i) {
            wchar_t ch = imeOverlay.text[i];
            char32_t cp = static_cast<char32_t>(ch);
            // Handle surrogate pairs
            if (i + 1 < imeOverlay.text.size() &&
                ch >= 0xD800 && ch <= 0xDBFF) {
                wchar_t lo = imeOverlay.text[i + 1];
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((ch - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                }
            }

            int w = codepoint_width(cp);
            if (w < 1) w = 1;
            if (imeCol + w > cols) break;

            // Draw opaque background quad to cover existing cell content
            for (int c = imeCol; c < imeCol + w; ++c) {
                D3DCellInstance bgInst = {};
                bgInst.position[0] = c * cellW + gridOffsetX;
                bgInst.position[1] = imeRow * cellH + gridOffsetY;
                colorFromRGBA(imeOverlay.bg_color, bgInst.bg_color);
                bgInst.flags = 4;  // is_bg_pass
                cellInstances.push_back(bgInst);
            }

            // Draw glyph for IME character
            auto faceId = fontCollection->resolveFace(cp);
            if (faceId != kInvalidCollectionFace) {
                auto rastFace = fontCollection->rasterizerFaceId(faceId);
                uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
                if (glyphIdx != 0) {
                    GlyphKey key{rastFace, glyphIdx, {0, 0}};
                    auto info = glyphCache->getOrRasterize(
                        key, fontSize, *rasterizer, *glyphAtlas);
                    if (info && info->region.width > 0 && info->region.height > 0) {
                        D3DCellInstance inst = {};
                        float offsetX = static_cast<float>(info->region.bearing_x);
                        float offsetY = ascent - static_cast<float>(info->region.bearing_y);

                        inst.position[0] = imeCol * cellW + offsetX + gridOffsetX;
                        inst.position[1] = imeRow * cellH + offsetY + gridOffsetY;
                        inst.atlas_uv[0] = static_cast<float>(info->region.x);
                        inst.atlas_uv[1] = static_cast<float>(info->region.y);
                        inst.atlas_size[0] = static_cast<float>(info->region.width);
                        inst.atlas_size[1] = static_cast<float>(info->region.height);

                        colorFromRGBA(imeOverlay.fg_color, inst.fg_color);
                        inst.flags = 1;  // has_glyph
                        if (info->is_color) inst.flags |= 2;
                        cellInstances.push_back(inst);
                    }
                }
            }

            imeCol += w;
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
                inst.position[0] = col * cellW + gridOffsetX;
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
            inst.position[0] = col * cellW + gridOffsetX;
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
    appendCursorInstances(screen, cellW, cellH, gridOffsetX, gridOffsetY);

    // Passes 5-9: Overlay elements (status bar, scrollbar, resize overlay,
    // tab bar with notification indicators, pane borders with glow).
    buildOverlayPasses(screen, cellW, cellH, ascent, fontSize);
}

template<typename ScreenT>
void D3DTextRenderer::Impl::appendCursorInstances(
        const ScreenT& screen, float cellW, float cellH, float gridOffsetX, float gridOffsetY) {
    int rows = screen.rows();
    int cols = screen.cols();

    if (screen.cursorVisible() && cursorBlinkVisible && !imeActive && screen.viewportOffset() == 0) {
        int cRow = screen.cursorRow();
        int cCol = screen.cursorCol();
        if (cRow >= 0 && cRow < rows && cCol >= 0 && cCol < cols) {
            const DynamicColors& colors = screen.dynamicColors();
            uint32_t cursorColor =
                colors.resolveFg(colors.cursor_color);

            CursorShape shape = screen.cursorShape();

            if (shape == CursorShape::Block) {
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW + gridOffsetX;
                inst.position[1] = cRow * cellH + gridOffsetY;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.bg_color[3] = 0.5f;
                inst.flags = 4;  // is_bg_pass
                cellInstances.push_back(inst);
            } else if (shape == CursorShape::Bar) {
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW + gridOffsetX;
                inst.position[1] = cRow * cellH + gridOffsetY;
                inst.atlas_size[0] = 2.0f;
                inst.atlas_size[1] = cellH;
                colorFromRGBA(cursorColor, inst.bg_color);
                inst.flags = 8;  // is_cursor
                cellInstances.push_back(inst);
            } else if (shape == CursorShape::Underline) {
                D3DCellInstance inst = {};
                inst.position[0] = cCol * cellW + gridOffsetX;
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

template<typename ScreenT>
void D3DTextRenderer::Impl::patchCursorOnly(const ScreenT& screen) {
    if (!fontCollection) return;

    FontMetrics metrics = fontCollection->primaryMetrics();
    float cellW = metrics.cell_width;
    float cellH = metrics.cell_height;

    float tabBarH = cellH * tabBar.height_scale;
    float gridOffsetY = (tabBar.visible && !tabBar.tabs.empty()) ? tabBarH : 0.0f;
    float gridOffsetX = sidebar.visible ? static_cast<float>(sidebar.width) : 0.0f;

    // Remove old cursor instances (everything from cursor onward, before overlays)
    cellInstances.resize(cellCountBeforeCursor);

    // Append fresh cursor instances
    appendCursorInstances(screen, cellW, cellH, gridOffsetX, gridOffsetY);

    // Re-append overlay passes
    float ascent = metrics.ascent;
    float fontSize = fontCollection->fontSize();
    buildOverlayPasses(screen, cellW, cellH, ascent, fontSize);
}

// Explicit template instantiations for Screen and ScreenSnapshot
template void D3DTextRenderer::Impl::buildCellBuffer<Screen>(const Screen&);
template void D3DTextRenderer::Impl::buildCellBuffer<::ScreenSnapshot>(const ::ScreenSnapshot&);
template void D3DTextRenderer::Impl::appendCursorInstances<Screen>(const Screen&, float, float, float, float);
template void D3DTextRenderer::Impl::appendCursorInstances<::ScreenSnapshot>(const ::ScreenSnapshot&, float, float, float, float);
template void D3DTextRenderer::Impl::patchCursorOnly<Screen>(const Screen&);
template void D3DTextRenderer::Impl::patchCursorOnly<::ScreenSnapshot>(const ::ScreenSnapshot&);

} // namespace termcore

#endif // _WIN32
