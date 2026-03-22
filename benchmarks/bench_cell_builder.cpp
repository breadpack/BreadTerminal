#include "bench_cell_builder.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"

namespace bench {

/// Simulates the cell builder logic without GPU dependencies.
/// Mirrors the structure of D3DCellBuilder but works with plain data.
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

static void colorFromRGBA(uint32_t rgba, float out[4]) {
    out[0] = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
    out[1] = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
    out[2] = static_cast<float>(rgba & 0xFF) / 255.0f;
    out[3] = 1.0f;
}

static void buildCellBuffer(const termcore::Screen& screen,
                             std::vector<CellInstance>& instances,
                             float cellW, float cellH) {
    int rows = screen.rows();
    int cols = screen.cols();

    instances.clear();
    instances.reserve(rows * cols * 2);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const auto& cell = screen.cellAt(r, c);

            // Background cell
            CellInstance bg{};
            bg.position[0] = static_cast<float>(c) * cellW;
            bg.position[1] = static_cast<float>(r) * cellH;
            colorFromRGBA(cell.bg_color, bg.bg_color);
            bg.flags = 4; // is_bg
            instances.push_back(bg);

            // Foreground (glyph) cell
            if (cell.codepoint != ' ' && cell.codepoint != 0) {
                CellInstance fg{};
                fg.position[0] = static_cast<float>(c) * cellW;
                fg.position[1] = static_cast<float>(r) * cellH;
                fg.atlas_uv[0] = 0;  // Would come from glyph cache
                fg.atlas_uv[1] = 0;
                fg.atlas_size[0] = cellW;
                fg.atlas_size[1] = cellH;
                fg.glyph_offset[0] = 0;
                fg.glyph_offset[1] = 0;
                colorFromRGBA(cell.fg_color, fg.fg_color);
                fg.flags = 1; // has_glyph
                if (cell.attributes & termcore::AttrUnderline) {
                    fg.flags |= 16; // is_underline
                    fg.extra_flags = cell.underline_style;
                }
                instances.push_back(fg);
            }
        }
    }
}

static void buildDirtyRegionBuffer(const termcore::Screen& screen,
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
            bg.flags = 4;
            instances.push_back(bg);

            if (cell.codepoint != ' ' && cell.codepoint != 0) {
                CellInstance fg{};
                fg.position[0] = static_cast<float>(c) * cellW;
                fg.position[1] = static_cast<float>(r) * cellH;
                colorFromRGBA(cell.fg_color, fg.fg_color);
                fg.flags = 1;
                instances.push_back(fg);
            }
        }
    }
}

/// Build overlay cells for a simulated status bar.
static void buildStatusBarOverlay(std::vector<CellInstance>& instances,
                                   int cols, float cellW, float cellH,
                                   float gridHeight) {
    float y = gridHeight;
    for (int c = 0; c < cols; ++c) {
        CellInstance bg{};
        bg.position[0] = static_cast<float>(c) * cellW;
        bg.position[1] = y;
        bg.bg_color[0] = 0.176f; // #2d2d2d
        bg.bg_color[1] = 0.176f;
        bg.bg_color[2] = 0.176f;
        bg.bg_color[3] = 1.0f;
        bg.flags = 4;
        instances.push_back(bg);
    }
    // Simulate some text cells in the status bar
    for (int c = 0; c < 20 && c < cols; ++c) {
        CellInstance fg{};
        fg.position[0] = static_cast<float>(c) * cellW;
        fg.position[1] = y;
        fg.fg_color[0] = 0.8f;
        fg.fg_color[1] = 0.8f;
        fg.fg_color[2] = 0.8f;
        fg.fg_color[3] = 1.0f;
        fg.flags = 1;
        instances.push_back(fg);
    }
}

/// Build overlay cells for a simulated tab bar.
static void buildTabBarOverlay(std::vector<CellInstance>& instances,
                                int cols, float cellW, float cellH,
                                int num_tabs) {
    float tabBarH = cellH * 1.4f;
    // Background
    for (int c = 0; c < cols; ++c) {
        CellInstance bg{};
        bg.position[0] = static_cast<float>(c) * cellW;
        bg.position[1] = 0;
        bg.bg_color[0] = 0.118f;
        bg.bg_color[1] = 0.118f;
        bg.bg_color[2] = 0.118f;
        bg.bg_color[3] = 1.0f;
        bg.flags = 4;
        instances.push_back(bg);
    }
    // Tab text cells
    int chars_per_tab = cols / std::max(num_tabs, 1);
    for (int tab = 0; tab < num_tabs; ++tab) {
        for (int c = 0; c < 10 && c < chars_per_tab; ++c) {
            CellInstance fg{};
            fg.position[0] = static_cast<float>(tab * chars_per_tab + c) * cellW;
            fg.position[1] = 0;
            fg.fg_color[0] = 0.8f;
            fg.fg_color[1] = 0.8f;
            fg.fg_color[2] = 0.8f;
            fg.fg_color[3] = 1.0f;
            fg.flags = 1;
            instances.push_back(fg);
        }
    }
}

void runCellBuilderBenchmarks(BenchmarkRunner& runner) {
    constexpr float cellW = 8.4f;
    constexpr float cellH = 18.0f;

    // --- Full screen cell buffer generation (80x24) ---
    {
        runner.runTimed("cell_build_80x24", "us", [&]() {
            termcore::Screen screen(24, 80);
            termcore::VtParser parser(screen);
            std::string content;
            for (int i = 0; i < 24; ++i) {
                content += "\033[1;33m";
                for (int j = 0; j < 76; ++j)
                    content.push_back(static_cast<char>(65 + ((i + j) % 26)));
                content += "\033[0m\r\n";
            }
            parser.feed(content.data(), content.size());

            std::vector<CellInstance> instances;
            buildCellBuffer(screen, instances, cellW, cellH);
        });
    }

    // --- Full screen cell buffer generation (200x50) ---
    {
        runner.runTimed("cell_build_200x50", "us", [&]() {
            termcore::Screen screen(50, 200);
            termcore::VtParser parser(screen);
            std::string content;
            for (int i = 0; i < 50; ++i) {
                content += "\033[38;2;100;200;50m";
                for (int j = 0; j < 196; ++j)
                    content.push_back(static_cast<char>(65 + ((i + j) % 26)));
                content += "\033[0m\r\n";
            }
            parser.feed(content.data(), content.size());

            std::vector<CellInstance> instances;
            buildCellBuffer(screen, instances, cellW, cellH);
        });
    }

    // --- Dirty region detection and partial rebuild ---
    {
        runner.runTimed("dirty_region_build_80x24", "us", [&]() {
            termcore::Screen screen(24, 80);
            termcore::VtParser parser(screen);

            // Fill screen
            std::string fill;
            for (int i = 0; i < 24; ++i) {
                for (int j = 0; j < 78; ++j)
                    fill.push_back(static_cast<char>(65 + ((i + j) % 26)));
                fill += "\r\n";
            }
            parser.feed(fill.data(), fill.size());
            screen.clearDirty();

            // Dirty a few rows (simulate cursor line update)
            std::string update = "\033[12;1HUpdated line content here";
            parser.feed(update.data(), update.size());

            std::vector<CellInstance> instances;
            buildDirtyRegionBuffer(screen, instances, cellW, cellH);
        });
    }

    // --- Status bar overlay building ---
    {
        runner.runTimed("overlay_status_bar", "us", [&]() {
            std::vector<CellInstance> instances;
            instances.reserve(200);
            buildStatusBarOverlay(instances, 80, cellW, cellH, 24.0f * cellH);
        });
    }

    // --- Tab bar overlay building ---
    {
        runner.runTimed("overlay_tab_bar_5_tabs", "us", [&]() {
            std::vector<CellInstance> instances;
            instances.reserve(200);
            buildTabBarOverlay(instances, 80, cellW, cellH, 5);
        });
    }

    // --- Combined overlay building (status + tab bar) ---
    {
        runner.runTimed("overlay_combined", "us", [&]() {
            std::vector<CellInstance> instances;
            instances.reserve(500);
            buildTabBarOverlay(instances, 200, cellW, cellH, 8);
            buildStatusBarOverlay(instances, 200, cellW, cellH, 50.0f * cellH);
        });
    }

    // --- Cell instance count estimation ---
    {
        runner.run("cell_instances_per_frame_200x50", "count", [&]() -> double {
            termcore::Screen screen(50, 200);
            termcore::VtParser parser(screen);
            std::string content;
            for (int i = 0; i < 50; ++i) {
                for (int j = 0; j < 198; ++j)
                    content.push_back(static_cast<char>(65 + ((i + j) % 26)));
                content += "\r\n";
            }
            parser.feed(content.data(), content.size());

            std::vector<CellInstance> instances;
            buildCellBuffer(screen, instances, cellW, cellH);
            return static_cast<double>(instances.size());
        });
    }
}

} // namespace bench
