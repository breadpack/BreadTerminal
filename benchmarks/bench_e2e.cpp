#include "bench_e2e.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/search.h"

namespace bench {

void runE2EBenchmarks(BenchmarkRunner& runner) {
    // --- Cat large file simulation ---
    // Feed 10MB of plain text through parser+screen (full pipeline)
    {
        constexpr size_t data_size = 10 * 1024 * 1024;
        std::string data = generateAsciiData(data_size);

        runner.run("cat_10mb_file", "MB/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);
            BenchmarkTimer t;
            t.start();
            // Feed in chunks to simulate realistic I/O
            constexpr size_t chunk_size = 16384;
            for (size_t offset = 0; offset < data.size(); offset += chunk_size) {
                size_t len = std::min(chunk_size, data.size() - offset);
                parser.feed(data.data() + offset, len);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(data_size) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- Cat with large screen ---
    {
        constexpr size_t data_size = 10 * 1024 * 1024;
        std::string data = generateAsciiData(data_size);

        runner.run("cat_10mb_200x50", "MB/s", [&]() -> double {
            termcore::Screen screen(50, 200);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);
            BenchmarkTimer t;
            t.start();
            constexpr size_t chunk_size = 16384;
            for (size_t offset = 0; offset < data.size(); offset += chunk_size) {
                size_t len = std::min(chunk_size, data.size() - offset);
                parser.feed(data.data() + offset, len);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(data_size) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- ls -la simulation ---
    {
        constexpr int num_lines = 10000;
        std::string data = generateLsOutput(num_lines);

        runner.run("ls_la_10k_lines", "MB/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);
            BenchmarkTimer t;
            t.start();
            constexpr size_t chunk_size = 8192;
            for (size_t offset = 0; offset < data.size(); offset += chunk_size) {
                size_t len = std::min(chunk_size, data.size() - offset);
                parser.feed(data.data() + offset, len);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(data.size()) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- Vim-style full screen redraw ---
    {
        std::string redraw = generateVimRedraw(24, 80);

        runner.run("vim_redraw_80x24", "redraws/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            termcore::VtParser parser(screen);
            constexpr int redraws = 500;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < redraws; ++i) {
                parser.feed(redraw.data(), redraw.size());
            }
            double sec = t.elapsedSec();
            return static_cast<double>(redraws) / sec;
        });
    }

    // --- Vim-style redraw on large screen ---
    {
        std::string redraw = generateVimRedraw(50, 200);

        runner.run("vim_redraw_200x50", "redraws/s", [&]() -> double {
            termcore::Screen screen(50, 200);
            termcore::VtParser parser(screen);
            constexpr int redraws = 100;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < redraws; ++i) {
                parser.feed(redraw.data(), redraw.size());
            }
            double sec = t.elapsedSec();
            return static_cast<double>(redraws) / sec;
        });
    }

    // --- Scrollback search ---
    {
        runner.runTimed("scrollback_search_10k_lines", "ms", [&]() {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);

            // Fill scrollback with varied content
            for (int i = 0; i < 10000; ++i) {
                std::string line = "Line " + std::to_string(i) + ": ";
                if (i % 100 == 42) {
                    line += "NEEDLE_FOUND_HERE ";
                }
                for (int j = static_cast<int>(line.size()); j < 78; ++j) {
                    line.push_back(static_cast<char>(97 + (j % 26)));
                }
                line += "\r\n";
                parser.feed(line.data(), line.size());
            }

            termcore::TerminalSearch search;
            termcore::SearchOptions opts;
            opts.search_scrollback = true;
            opts.case_sensitive = true;
            search.search(screen, "NEEDLE_FOUND_HERE", opts);
        });
    }

    // --- Scrollback search (case insensitive) ---
    {
        runner.runTimed("scrollback_search_nocase_10k", "ms", [&]() {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);

            for (int i = 0; i < 10000; ++i) {
                std::string line = "Line " + std::to_string(i) + ": ";
                if (i % 50 == 0) {
                    line += "SearchTarget ";
                }
                for (int j = static_cast<int>(line.size()); j < 78; ++j) {
                    line.push_back(static_cast<char>(97 + (j % 26)));
                }
                line += "\r\n";
                parser.feed(line.data(), line.size());
            }

            termcore::TerminalSearch search;
            termcore::SearchOptions opts;
            opts.search_scrollback = true;
            opts.case_sensitive = false;
            search.search(screen, "searchtarget", opts);
        });
    }

    // --- Unicode heavy workload (e2e) ---
    {
        constexpr size_t data_size = 2 * 1024 * 1024;
        std::string data = generateUnicodeData(data_size);

        runner.run("unicode_e2e_2mb", "MB/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(5000);
            termcore::VtParser parser(screen);
            BenchmarkTimer t;
            t.start();
            constexpr size_t chunk_size = 8192;
            for (size_t offset = 0; offset < data.size(); offset += chunk_size) {
                size_t len = std::min(chunk_size, data.size() - offset);
                parser.feed(data.data() + offset, len);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(data.size()) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- Color-heavy workload (e2e) ---
    {
        constexpr size_t data_size = 4 * 1024 * 1024;
        std::string data = generateColorData(data_size);

        runner.run("color_e2e_4mb", "MB/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);
            BenchmarkTimer t;
            t.start();
            constexpr size_t chunk_size = 16384;
            for (size_t offset = 0; offset < data.size(); offset += chunk_size) {
                size_t len = std::min(chunk_size, data.size() - offset);
                parser.feed(data.data() + offset, len);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(data.size()) / (1024.0 * 1024.0)) / sec;
        });
    }
}

} // namespace bench
