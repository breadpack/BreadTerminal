#include "bench_screen.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/search.h"
#include "termcore/selection_manager.h"

namespace bench {

void runScreenBenchmarks(BenchmarkRunner& runner) {
    // --- Cell write speed ---
    {
        runner.run("cell_write_speed", "Mops/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            constexpr int ops = 24 * 80 * 100; // Fill screen 100 times
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                int row = i % 24;
                int col = (i / 24) % 80;
                auto& cell = screen.mutableCellAt(row, col);
                cell.codepoint = static_cast<char32_t>(65 + (i % 26));
                cell.fg_color = 0xFFFFFF;
                cell.bg_color = 0x000000;
                cell.attributes = 0;
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(ops) / 1e6) / sec;
        });
    }

    // --- Screen resize ---
    {
        runner.runTimed("screen_resize_80x24_to_200x50", "us", [&]() {
            termcore::Screen screen(24, 80);
            // Write some content first
            termcore::VtParser parser(screen);
            std::string content;
            for (int i = 0; i < 24; ++i) {
                for (int j = 0; j < 78; ++j)
                    content.push_back(static_cast<char>(65 + ((i + j) % 26)));
                content += "\r\n";
            }
            parser.feed(content.data(), content.size());
            screen.resize(50, 200);
        });

        runner.runTimed("screen_resize_200x50_to_80x24", "us", [&]() {
            termcore::Screen screen(50, 200);
            termcore::VtParser parser(screen);
            std::string content;
            for (int i = 0; i < 50; ++i) {
                for (int j = 0; j < 198; ++j)
                    content.push_back(static_cast<char>(65 + ((i + j) % 26)));
                content += "\r\n";
            }
            parser.feed(content.data(), content.size());
            screen.resize(24, 80);
        });

        runner.runTimed("screen_resize_round_trip", "us", [&]() {
            termcore::Screen screen(24, 80);
            screen.resize(200, 50);
            screen.resize(24, 80);
        });
    }

    // --- Scroll performance ---
    {
        runner.run("scroll_ops_per_sec", "Kops/s", [&]() -> double {
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

            // Scroll by writing lines that push content up
            constexpr int scroll_ops = 10000;
            std::string line(78, 'A');
            line += "\r\n";

            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < scroll_ops; ++i) {
                parser.feed(line.data(), line.size());
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(scroll_ops) / 1000.0) / sec;
        });
    }

    // --- Viewport scroll performance ---
    {
        runner.runTimed("viewport_scroll_1000_lines", "us", [&]() {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);

            // Fill scrollback
            std::string data;
            for (int i = 0; i < 5000; ++i) {
                for (int j = 0; j < 78; ++j)
                    data.push_back(static_cast<char>(65 + ((i + j) % 26)));
                data += "\r\n";
            }
            parser.feed(data.data(), data.size());

            // Scroll viewport up and down
            for (int i = 0; i < 1000; ++i) {
                screen.scrollViewportUp(1);
            }
            for (int i = 0; i < 1000; ++i) {
                screen.scrollViewportDown(1);
            }
        });
    }

    // --- Selection text extraction ---
    {
        runner.runTimed("selection_extract_full_screen", "us", [&]() {
            termcore::Screen screen(50, 200);
            termcore::VtParser parser(screen);

            // Fill large screen with content
            std::string content;
            for (int i = 0; i < 50; ++i) {
                for (int j = 0; j < 198; ++j)
                    content.push_back(static_cast<char>(65 + ((i + j) % 26)));
                content += "\r\n";
            }
            parser.feed(content.data(), content.size());

            // Extract text from selection spanning full screen
            termcore::SelectionManager sel;
            sel.selectAll(50, 200);
            volatile auto text = sel.getSelectedText(screen);
            (void)text;
        });
    }

    // --- Line text extraction ---
    {
        runner.run("line_text_extract", "Kops/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            termcore::VtParser parser(screen);
            std::string fill;
            for (int i = 0; i < 24; ++i) {
                for (int j = 0; j < 78; ++j)
                    fill.push_back(static_cast<char>(65 + ((i + j) % 26)));
                fill += "\r\n";
            }
            parser.feed(fill.data(), fill.size());

            constexpr int ops = 10000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                volatile auto text = screen.getLineText(i % 24);
                (void)text;
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(ops) / 1000.0) / sec;
        });
    }

    // --- Dirty tracking ---
    {
        runner.runTimed("dirty_tracking_check_all_rows", "us", [&]() {
            termcore::Screen screen(200, 200);
            termcore::VtParser parser(screen);
            // Write some data to dirty some rows
            std::string data = "Hello World\r\n";
            for (int i = 0; i < 50; ++i)
                parser.feed(data.data(), data.size());

            // Check dirty state for all rows
            for (int i = 0; i < 200; ++i) {
                volatile bool dirty = screen.isRowDirty(i);
                (void)dirty;
            }
            screen.clearDirty();
        });
    }

    // --- Scrollback memory estimation ---
    {
        runner.run("scrollback_memory_per_line", "bytes/line", [&]() -> double {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);

            // Fill scrollback to capacity
            std::string line(78, 'X');
            line += "\r\n";

            for (int i = 0; i < 11000; ++i) {
                parser.feed(line.data(), line.size());
            }

            // Estimate: each scrollback line is a vector<TermCell> of 80 cells
            // TermCell is: char32_t(4) + uint32_t(4) + uint32_t(4) + uint16_t(2)
            //            + uint8_t(1) + uint8_t(1) + uint32_t(4) = 20 bytes
            // Plus vector overhead (~24 bytes)
            size_t sb_size = screen.scrollbackSize();
            double estimated_bytes = static_cast<double>(sb_size) *
                (sizeof(termcore::TermCell) * 80 + 24);
            return estimated_bytes / static_cast<double>(sb_size);
        });
    }
}

} // namespace bench
