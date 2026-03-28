#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/term_cell.h"
#include "termcore/dynamic_colors.h"

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {
namespace {

// ---------------------------------------------------------------------------
// Standalone cell builder logic -- mirrors D3DCellBuilder / bench_cell_builder
// without any D3D or GPU dependency.
// ---------------------------------------------------------------------------

/// GPU-compatible cell instance (matches D3DCellInstance layout).
struct CellInstance {
    float position[2];
    float atlas_uv[2];
    float atlas_size[2];
    float glyph_offset[2];
    float fg_color[4];
    float bg_color[4];
    uint32_t flags;
    uint32_t extra_flags;
};

// Flag bits matching D3DCellInstance.flags
static constexpr uint32_t kFlagHasGlyph   = 1;
static constexpr uint32_t kFlagIsColor    = 2;
static constexpr uint32_t kFlagIsBg       = 4;
static constexpr uint32_t kFlagIsCursor   = 8;
static constexpr uint32_t kFlagIsUnderline = 16;

/// Selection state (mirrors D3DTextRenderer::Selection).
struct Selection {
    int startRow = 0;
    int startCol = 0;
    int endRow = 0;
    int endCol = 0;
    bool active = false;
};

static void colorFromRGBA(uint32_t rgba, float out[4]) {
    out[0] = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
    out[1] = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
    out[2] = static_cast<float>(rgba & 0xFF) / 255.0f;
    out[3] = 1.0f;
}

static bool isCellSelected(const Selection& sel, int row, int col) {
    if (!sel.active) return false;
    int sr = sel.startRow, sc = sel.startCol;
    int er = sel.endRow, ec = sel.endCol;
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

/// Build cell instances from a Screen (pure logic, no GPU).
/// Produces background cells for every position and foreground cells for
/// non-empty positions.  Also generates cursor and selection overlays.
static void buildCellBuffer(const Screen& screen,
                            std::vector<CellInstance>& instances,
                            float cellW, float cellH,
                            const Selection& sel = {},
                            bool cursorVisible = true) {
    int rows = screen.rows();
    int cols = screen.cols();
    const auto& dyn = screen.dynamicColors();

    instances.clear();
    instances.reserve(rows * cols * 2);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const auto& cell = screen.cellAt(r, c);

            uint32_t resolvedBg = dyn.resolveBg(cell.bg_color);
            uint32_t resolvedFg = dyn.resolveFg(cell.fg_color);

            // Handle inverse attribute
            if (cell.attributes & AttrInverse) {
                std::swap(resolvedBg, resolvedFg);
            }

            // Background cell
            CellInstance bg{};
            bg.position[0] = static_cast<float>(c) * cellW;
            bg.position[1] = static_cast<float>(r) * cellH;
            colorFromRGBA(resolvedBg, bg.bg_color);
            bg.flags = kFlagIsBg;

            // Selection highlight: blend selection color onto bg
            if (isCellSelected(sel, r, c)) {
                // Use a fixed selection highlight color (blue)
                bg.bg_color[0] = 0.0f;
                bg.bg_color[1] = 0.478f;
                bg.bg_color[2] = 0.8f;
                bg.bg_color[3] = 1.0f;
            }

            instances.push_back(bg);

            // Cursor cell
            if (cursorVisible &&
                r == screen.cursorRow() && c == screen.cursorCol()) {
                CellInstance cur{};
                cur.position[0] = static_cast<float>(c) * cellW;
                cur.position[1] = static_cast<float>(r) * cellH;
                cur.flags = kFlagIsCursor;
                colorFromRGBA(dyn.cursor_color, cur.fg_color);
                instances.push_back(cur);
            }

            // Foreground (glyph) cell -- skip continuation cells of wide chars
            if (cell.width == 0) continue;  // continuation of wide char
            if (cell.codepoint != ' ' && cell.codepoint != 0) {
                CellInstance fg{};
                fg.position[0] = static_cast<float>(c) * cellW;
                fg.position[1] = static_cast<float>(r) * cellH;
                fg.atlas_size[0] = cellW * cell.width;  // wide char spans
                fg.atlas_size[1] = cellH;
                colorFromRGBA(resolvedFg, fg.fg_color);
                fg.flags = kFlagHasGlyph;
                if (cell.attributes & AttrUnderline) {
                    fg.flags |= kFlagIsUnderline;
                    fg.extra_flags = cell.underline_style;
                }
                instances.push_back(fg);
            }
        }
    }
}

/// Build cell instances using dirty-row optimization.
static void buildDirtyRegionBuffer(const Screen& screen,
                                    std::vector<CellInstance>& instances,
                                    float cellW, float cellH) {
    int rows = screen.rows();
    int cols = screen.cols();

    instances.clear();
    instances.reserve(rows * cols);

    for (int r = 0; r < rows; ++r) {
        if (!screen.isRowDirty(r)) continue;

        for (int c = 0; c < cols; ++c) {
            const auto& cell = screen.cellAt(r, c);

            CellInstance bg{};
            bg.position[0] = static_cast<float>(c) * cellW;
            bg.position[1] = static_cast<float>(r) * cellH;
            colorFromRGBA(cell.bg_color, bg.bg_color);
            bg.flags = kFlagIsBg;
            instances.push_back(bg);

            if (cell.codepoint != ' ' && cell.codepoint != 0) {
                CellInstance fg{};
                fg.position[0] = static_cast<float>(c) * cellW;
                fg.position[1] = static_cast<float>(r) * cellH;
                colorFromRGBA(cell.fg_color, fg.fg_color);
                fg.flags = kFlagHasGlyph;
                instances.push_back(fg);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static constexpr float kCellW = 8.4f;
static constexpr float kCellH = 18.0f;

class CellBuilderTest : public ::testing::Test {
protected:
    Screen screen{24, 80};
    std::vector<CellInstance> instances;

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }

    /// Count instances matching a predicate.
    int countIf(auto pred) const {
        int n = 0;
        for (const auto& ci : instances)
            if (pred(ci)) ++n;
        return n;
    }

    int countBg() const {
        return countIf([](const CellInstance& ci) {
            return (ci.flags & kFlagIsBg) != 0;
        });
    }

    int countFg() const {
        return countIf([](const CellInstance& ci) {
            return (ci.flags & kFlagHasGlyph) != 0;
        });
    }

    int countCursor() const {
        return countIf([](const CellInstance& ci) {
            return (ci.flags & kFlagIsCursor) != 0;
        });
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(CellBuilderTest, EmptyScreenProducesBackgroundCells) {
    // Empty screen with no text -- only bg cells
    buildCellBuffer(screen, instances, kCellW, kCellH);

    // Every cell position gets a bg instance
    EXPECT_EQ(countBg(), 24 * 80);

    // No text content -> no foreground glyph instances
    EXPECT_EQ(countFg(), 0);

    // Cursor is at (0,0) and visible by default
    EXPECT_EQ(countCursor(), 1);
}

TEST_F(CellBuilderTest, TextCellsHaveCorrectPositions) {
    feed("ABC");

    buildCellBuffer(screen, instances, kCellW, kCellH);

    // Find the foreground cell for 'A' (col 0, row 0)
    const CellInstance* cellA = nullptr;
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagHasGlyph) && ci.position[0] == 0.0f && ci.position[1] == 0.0f) {
            cellA = &ci;
            break;
        }
    }
    ASSERT_NE(cellA, nullptr) << "Expected glyph cell at (0,0)";
    EXPECT_FLOAT_EQ(cellA->position[0], 0.0f);
    EXPECT_FLOAT_EQ(cellA->position[1], 0.0f);

    // Find 'B' at col 1
    const CellInstance* cellB = nullptr;
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagHasGlyph) && ci.position[0] == kCellW) {
            cellB = &ci;
            break;
        }
    }
    ASSERT_NE(cellB, nullptr) << "Expected glyph cell at col 1";
    EXPECT_FLOAT_EQ(cellB->position[0], kCellW);
    EXPECT_FLOAT_EQ(cellB->position[1], 0.0f);

    // Find 'C' at col 2
    const CellInstance* cellC = nullptr;
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagHasGlyph) && ci.position[0] == 2.0f * kCellW) {
            cellC = &ci;
            break;
        }
    }
    ASSERT_NE(cellC, nullptr) << "Expected glyph cell at col 2";
    EXPECT_FLOAT_EQ(cellC->position[0], 2.0f * kCellW);

    // Exactly 3 foreground cells
    EXPECT_EQ(countFg(), 3);
}

TEST_F(CellBuilderTest, SGRColorsApplied) {
    // Bold bright red foreground: SGR 1;31
    // (Bright red = palette index 9 = 0xFF5555 on many terms,
    //  but with kColorDefault, the actual resolved color depends on the palette.)
    // Use explicit 24-bit color for deterministic testing.
    feed("\033[38;2;255;0;0mR\033[38;2;0;255;0mG\033[0m");

    buildCellBuffer(screen, instances, kCellW, kCellH);

    // Find the red 'R' glyph at col 0
    const CellInstance* red = nullptr;
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagHasGlyph) && ci.position[0] == 0.0f) {
            red = &ci;
            break;
        }
    }
    ASSERT_NE(red, nullptr);
    EXPECT_NEAR(red->fg_color[0], 1.0f, 0.01f);  // R=255
    EXPECT_NEAR(red->fg_color[1], 0.0f, 0.01f);  // G=0
    EXPECT_NEAR(red->fg_color[2], 0.0f, 0.01f);  // B=0

    // Find the green 'G' glyph at col 1
    const CellInstance* green = nullptr;
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagHasGlyph) && ci.position[0] == kCellW) {
            green = &ci;
            break;
        }
    }
    ASSERT_NE(green, nullptr);
    EXPECT_NEAR(green->fg_color[0], 0.0f, 0.01f);
    EXPECT_NEAR(green->fg_color[1], 1.0f, 0.01f);
    EXPECT_NEAR(green->fg_color[2], 0.0f, 0.01f);
}

TEST_F(CellBuilderTest, SGRBackgroundColorApplied) {
    // Set bg to blue (24-bit)
    feed("\033[48;2;0;0;255m \033[0m");

    buildCellBuffer(screen, instances, kCellW, kCellH);

    // Find bg cell at col 0, row 0
    const CellInstance* bgCell = nullptr;
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagIsBg) && ci.position[0] == 0.0f && ci.position[1] == 0.0f) {
            bgCell = &ci;
            break;
        }
    }
    ASSERT_NE(bgCell, nullptr);
    EXPECT_NEAR(bgCell->bg_color[0], 0.0f, 0.01f);
    EXPECT_NEAR(bgCell->bg_color[1], 0.0f, 0.01f);
    EXPECT_NEAR(bgCell->bg_color[2], 1.0f, 0.01f);
}

TEST_F(CellBuilderTest, CursorCellGenerated) {
    feed("Hello");
    // Cursor should be at (0, 5) after printing "Hello"

    buildCellBuffer(screen, instances, kCellW, kCellH);

    EXPECT_EQ(countCursor(), 1);

    // Find cursor cell and verify position
    for (const auto& ci : instances) {
        if (ci.flags & kFlagIsCursor) {
            EXPECT_FLOAT_EQ(ci.position[0], 5.0f * kCellW);
            EXPECT_FLOAT_EQ(ci.position[1], 0.0f);
            break;
        }
    }
}

TEST_F(CellBuilderTest, CursorHiddenProducesNoCursorCell) {
    feed("Hello");

    buildCellBuffer(screen, instances, kCellW, kCellH, {}, /*cursorVisible=*/false);

    EXPECT_EQ(countCursor(), 0);
}

TEST_F(CellBuilderTest, SelectionHighlight) {
    feed("Hello World");

    Selection sel;
    sel.active = true;
    sel.startRow = 0;
    sel.startCol = 0;
    sel.endRow = 0;
    sel.endCol = 4;  // Select "Hello"

    buildCellBuffer(screen, instances, kCellW, kCellH, sel);

    // Check that selected bg cells have the selection color
    int selectedBgCount = 0;
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagIsBg) && ci.position[1] == 0.0f) {
            int col = static_cast<int>(ci.position[0] / kCellW + 0.5f);
            if (col >= 0 && col <= 4) {
                // Should have the selection blue color
                EXPECT_NEAR(ci.bg_color[0], 0.0f, 0.01f);
                EXPECT_NEAR(ci.bg_color[1], 0.478f, 0.01f);
                EXPECT_NEAR(ci.bg_color[2], 0.8f, 0.01f);
                ++selectedBgCount;
            }
        }
    }
    EXPECT_EQ(selectedBgCount, 5);  // columns 0-4
}

TEST_F(CellBuilderTest, SelectionReversedOrder) {
    // Selection with end before start (user dragged backwards)
    feed("ABCDEFGH");

    Selection sel;
    sel.active = true;
    sel.startRow = 0;
    sel.startCol = 5;
    sel.endRow = 0;
    sel.endCol = 2;

    buildCellBuffer(screen, instances, kCellW, kCellH, sel);

    // Cols 2-5 should be selected
    int selectedCount = 0;
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagIsBg) && ci.position[1] == 0.0f) {
            int col = static_cast<int>(ci.position[0] / kCellW + 0.5f);
            if (col >= 2 && col <= 5) {
                EXPECT_NEAR(ci.bg_color[2], 0.8f, 0.01f);
                ++selectedCount;
            }
        }
    }
    EXPECT_EQ(selectedCount, 4);
}

TEST_F(CellBuilderTest, SelectionMultipleRows) {
    feed("Line one\r\nLine two\r\nLine three");

    Selection sel;
    sel.active = true;
    sel.startRow = 0;
    sel.startCol = 5;
    sel.endRow = 2;
    sel.endCol = 3;

    buildCellBuffer(screen, instances, kCellW, kCellH, sel);

    // Row 0: cols >= 5 should be selected
    EXPECT_TRUE(isCellSelected(sel, 0, 5));
    EXPECT_TRUE(isCellSelected(sel, 0, 79));
    EXPECT_FALSE(isCellSelected(sel, 0, 4));

    // Row 1: entire row should be selected
    EXPECT_TRUE(isCellSelected(sel, 1, 0));
    EXPECT_TRUE(isCellSelected(sel, 1, 79));

    // Row 2: cols <= 3 should be selected
    EXPECT_TRUE(isCellSelected(sel, 2, 0));
    EXPECT_TRUE(isCellSelected(sel, 2, 3));
    EXPECT_FALSE(isCellSelected(sel, 2, 4));
}

TEST_F(CellBuilderTest, WideCharSpansTwoCells) {
    // Write a CJK character (U+4E16 = width 2)
    feed("\xe4\xb8\x96");  // UTF-8 for U+4E16 (世)

    buildCellBuffer(screen, instances, kCellW, kCellH);

    // Should produce exactly 1 foreground glyph cell with double width
    int fgCount = 0;
    const CellInstance* wideGlyph = nullptr;
    for (const auto& ci : instances) {
        if (ci.flags & kFlagHasGlyph) {
            ++fgCount;
            wideGlyph = &ci;
        }
    }
    EXPECT_EQ(fgCount, 1);
    ASSERT_NE(wideGlyph, nullptr);

    // The atlas_size should reflect double width
    EXPECT_FLOAT_EQ(wideGlyph->atlas_size[0], kCellW * 2.0f);
    EXPECT_FLOAT_EQ(wideGlyph->atlas_size[1], kCellH);

    // Position at col 0
    EXPECT_FLOAT_EQ(wideGlyph->position[0], 0.0f);
}

TEST_F(CellBuilderTest, UnderlineAttributeProducesFlag) {
    // SGR 4 sets underline
    feed("\033[4mUnderlined\033[0m");

    buildCellBuffer(screen, instances, kCellW, kCellH);

    // Find the first glyph cell (should be 'U')
    for (const auto& ci : instances) {
        if (ci.flags & kFlagHasGlyph) {
            EXPECT_TRUE(ci.flags & kFlagIsUnderline)
                << "First glyph cell should have underline flag";
            break;
        }
    }
}

TEST_F(CellBuilderTest, UnderlineStylePreserved) {
    // SGR 4:3 = curly underline
    feed("\033[4:3mCurly\033[0m");

    buildCellBuffer(screen, instances, kCellW, kCellH);

    for (const auto& ci : instances) {
        if (ci.flags & kFlagHasGlyph) {
            EXPECT_TRUE(ci.flags & kFlagIsUnderline);
            EXPECT_EQ(ci.extra_flags, UnderlineCurly);
            break;
        }
    }
}

TEST_F(CellBuilderTest, InverseAttributeSwapsFgBg) {
    // Set explicit fg=red, bg=blue, then inverse
    feed("\033[38;2;255;0;0;48;2;0;0;255;7mX\033[0m");

    buildCellBuffer(screen, instances, kCellW, kCellH);

    // Find bg cell at (0,0) -- should have fg color (red) due to inverse
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagIsBg) && ci.position[0] == 0.0f && ci.position[1] == 0.0f) {
            EXPECT_NEAR(ci.bg_color[0], 1.0f, 0.01f);  // red
            EXPECT_NEAR(ci.bg_color[1], 0.0f, 0.01f);
            EXPECT_NEAR(ci.bg_color[2], 0.0f, 0.01f);
            break;
        }
    }

    // Find fg glyph at (0,0) -- should have bg color (blue) due to inverse
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagHasGlyph) && ci.position[0] == 0.0f) {
            EXPECT_NEAR(ci.fg_color[0], 0.0f, 0.01f);
            EXPECT_NEAR(ci.fg_color[1], 0.0f, 0.01f);
            EXPECT_NEAR(ci.fg_color[2], 1.0f, 0.01f);  // blue
            break;
        }
    }
}

TEST_F(CellBuilderTest, DirtyRowOptimization) {
    // Fill entire screen
    std::string fill;
    for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 78; ++j)
            fill.push_back(static_cast<char>('A' + ((i + j) % 26)));
        fill += "\r\n";
    }
    feed(fill);
    screen.clearDirty();

    // Dirty only row 10
    {
        VtParser parser(screen);
        std::string update = "\033[11;1HNew content";
        parser.feed(update.data(), update.size());
    }

    buildDirtyRegionBuffer(screen, instances, kCellW, kCellH);

    // Only row 10 (0-indexed) should have instances
    for (const auto& ci : instances) {
        float expectedY = 10.0f * kCellH;
        // Also cursor row might be dirty
        int row = static_cast<int>(ci.position[1] / kCellH + 0.5f);
        EXPECT_TRUE(screen.isRowDirty(row))
            << "Cell at row " << row << " but that row is not dirty";
    }

    // Should have far fewer instances than full buffer (80 cols * 24 rows * 2)
    std::vector<CellInstance> fullInstances;
    buildCellBuffer(screen, fullInstances, kCellW, kCellH);
    EXPECT_LT(instances.size(), fullInstances.size());
}

TEST_F(CellBuilderTest, DefaultColorsResolved) {
    // Characters with default colors should use DynamicColors
    feed("A");

    buildCellBuffer(screen, instances, kCellW, kCellH);

    const auto& dyn = screen.dynamicColors();
    float expectedFg[4];
    colorFromRGBA(dyn.foreground, expectedFg);

    // Find glyph cell for 'A'
    for (const auto& ci : instances) {
        if (ci.flags & kFlagHasGlyph) {
            EXPECT_NEAR(ci.fg_color[0], expectedFg[0], 0.01f);
            EXPECT_NEAR(ci.fg_color[1], expectedFg[1], 0.01f);
            EXPECT_NEAR(ci.fg_color[2], expectedFg[2], 0.01f);
            break;
        }
    }

    float expectedBg[4];
    colorFromRGBA(dyn.background, expectedBg);

    // Find bg cell at (0,0)
    for (const auto& ci : instances) {
        if ((ci.flags & kFlagIsBg) && ci.position[0] == 0.0f && ci.position[1] == 0.0f) {
            EXPECT_NEAR(ci.bg_color[0], expectedBg[0], 0.01f);
            EXPECT_NEAR(ci.bg_color[1], expectedBg[1], 0.01f);
            EXPECT_NEAR(ci.bg_color[2], expectedBg[2], 0.01f);
            break;
        }
    }
}

TEST_F(CellBuilderTest, SmallScreenProducesCorrectCount) {
    Screen small(2, 3);
    VtParser parser(small);
    std::string data = "AB";
    parser.feed(data.data(), data.size());

    std::vector<CellInstance> inst;
    buildCellBuffer(small, inst, kCellW, kCellH);

    // 2*3 = 6 bg cells + 2 fg cells + 1 cursor cell = 9
    int bg = 0, fg = 0, cur = 0;
    for (const auto& ci : inst) {
        if (ci.flags & kFlagIsBg) ++bg;
        if (ci.flags & kFlagHasGlyph) ++fg;
        if (ci.flags & kFlagIsCursor) ++cur;
    }
    EXPECT_EQ(bg, 6);
    EXPECT_EQ(fg, 2);
    EXPECT_EQ(cur, 1);
}

TEST_F(CellBuilderTest, FullScreenTextProducesCorrectCounts) {
    // Fill entire screen with text
    std::string fill;
    for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 80; ++j)
            fill.push_back(static_cast<char>('A' + ((i + j) % 26)));
        if (i < 23) fill += "\r\n";
    }
    feed(fill);

    buildCellBuffer(screen, instances, kCellW, kCellH);

    // 24*80 bg cells + 24*80 fg cells + 1 cursor
    EXPECT_EQ(countBg(), 24 * 80);
    EXPECT_EQ(countFg(), 24 * 80);
    EXPECT_EQ(countCursor(), 1);
}

// ---------------------------------------------------------------------------
// ScreenSnapshot tests (Windows-only, since ScreenSnapshot is _WIN32 guarded)
// ---------------------------------------------------------------------------

#if defined(_WIN32)
#include "ScreenSnapshot.h"

class ScreenSnapshotTest : public ::testing::Test {
protected:
    Screen screen{24, 80};

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }
};

TEST_F(ScreenSnapshotTest, CapturesAllCells) {
    feed("Hello World");

    ScreenSnapshot snap;
    snap.captureFrom(screen);

    EXPECT_EQ(snap.rows(), 24);
    EXPECT_EQ(snap.cols(), 80);

    // Verify captured cell content matches Screen
    EXPECT_EQ(snap.cellAt(0, 0).codepoint, U'H');
    EXPECT_EQ(snap.cellAt(0, 4).codepoint, U'o');
    EXPECT_EQ(snap.cellAt(0, 5).codepoint, U' ');
    EXPECT_EQ(snap.cellAt(0, 6).codepoint, U'W');
}

TEST_F(ScreenSnapshotTest, CapturesCursorState) {
    feed("ABC");

    ScreenSnapshot snap;
    snap.captureFrom(screen);

    EXPECT_EQ(snap.cursorRow(), 0);
    EXPECT_EQ(snap.cursorCol(), 3);
    EXPECT_TRUE(snap.cursorVisible());
    EXPECT_EQ(snap.cursorShape(), CursorShape::Block);
}

TEST_F(ScreenSnapshotTest, CapturesColors) {
    // Set explicit 24-bit color
    feed("\033[38;2;100;200;50mX\033[0m");

    ScreenSnapshot snap;
    snap.captureFrom(screen);

    const auto& cell = snap.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'X');
    // fg_color should be 0x64C832 (100,200,50 in RGB)
    uint32_t expectedFg = (100 << 16) | (200 << 8) | 50;
    EXPECT_EQ(cell.fg_color, expectedFg);
}

TEST_F(ScreenSnapshotTest, DirtyRowOptimization) {
    // Fill screen, clear dirty, then modify one row
    std::string fill;
    for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 78; ++j)
            fill.push_back(static_cast<char>('A' + ((i + j) % 26)));
        fill += "\r\n";
    }
    feed(fill);

    // First capture (full)
    ScreenSnapshot snap;
    snap.captureFrom(screen);
    EXPECT_TRUE(snap.isDirty());

    // Clear dirty, modify one row
    screen.clearDirty();
    {
        VtParser parser(screen);
        std::string update = "\033[5;1HChanged";
        parser.feed(update.data(), update.size());
    }

    // Incremental capture
    snap.captureFrom(screen);

    // Row 4 (0-indexed) should be dirty
    EXPECT_TRUE(snap.isRowDirty(4));
    // Verify the content was captured
    EXPECT_EQ(snap.cellAt(4, 0).codepoint, U'C');
    EXPECT_EQ(snap.cellAt(4, 1).codepoint, U'h');
}

TEST_F(ScreenSnapshotTest, IncrementalCapturePreservesCleanRows) {
    // Fill screen
    std::string fill;
    for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 78; ++j)
            fill.push_back(static_cast<char>('A' + ((i + j) % 26)));
        fill += "\r\n";
    }
    feed(fill);

    // First full capture
    ScreenSnapshot snap;
    snap.captureFrom(screen);

    // Remember content of row 0
    char32_t row0col0 = snap.cellAt(0, 0).codepoint;

    // Clear dirty, modify row 10 only
    screen.clearDirty();
    {
        VtParser parser(screen);
        std::string update = "\033[11;1HXYZ";
        parser.feed(update.data(), update.size());
    }

    // Incremental capture
    snap.captureFrom(screen);

    // Row 0 should still have old content (preserved from previous capture)
    EXPECT_EQ(snap.cellAt(0, 0).codepoint, row0col0);
    // Row 10 should have new content
    EXPECT_EQ(snap.cellAt(10, 0).codepoint, U'X');
    EXPECT_EQ(snap.cellAt(10, 1).codepoint, U'Y');
    EXPECT_EQ(snap.cellAt(10, 2).codepoint, U'Z');
}

TEST_F(ScreenSnapshotTest, ResizeTriggerFullCopy) {
    feed("ABCDE");

    ScreenSnapshot snap;
    snap.captureFrom(screen);
    EXPECT_EQ(snap.rows(), 24);
    EXPECT_EQ(snap.cols(), 80);

    // Resize screen
    screen.resize(10, 40);
    screen.clearDirty();
    {
        VtParser parser(screen);
        parser.feed("Z", 1);
    }

    // Capture after resize -- should do full copy despite dirty tracking
    snap.captureFrom(screen);
    EXPECT_EQ(snap.rows(), 10);
    EXPECT_EQ(snap.cols(), 40);
}

TEST_F(ScreenSnapshotTest, ViewportOffset) {
    // Generate enough output to create scrollback
    std::string longOutput;
    for (int i = 0; i < 50; ++i) {
        longOutput += "Line " + std::to_string(i) + "\r\n";
    }
    feed(longOutput);

    ScreenSnapshot snap;
    snap.captureFrom(screen);

    // Screen should have scrollback
    EXPECT_GT(snap.scrollbackSize(), 0u);
    EXPECT_EQ(snap.viewportOffset(), 0);  // at bottom
}

TEST_F(ScreenSnapshotTest, DynamicColorsCapture) {
    ScreenSnapshot snap;
    snap.captureFrom(screen);

    // Default colors should match
    EXPECT_EQ(snap.dynamicColors().foreground, screen.dynamicColors().foreground);
    EXPECT_EQ(snap.dynamicColors().background, screen.dynamicColors().background);
}

#endif // _WIN32

} // namespace
} // namespace termcore
