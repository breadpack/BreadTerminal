#include "bench_scrollback.h"
#include "termcore/scrollback_ring.h"
#include <random>

namespace bench {

/// Build a row of TermCells with simple ASCII content.
static std::vector<termcore::TermCell> makeRow(int cols, int seed) {
    std::vector<termcore::TermCell> row(cols);
    for (int c = 0; c < cols; ++c) {
        auto& cell = row[c];
        cell.codepoint = static_cast<char32_t>('A' + ((seed + c) % 26));
        cell.width = 1;
        cell.fg_color = 0xFFFFFF;
        cell.bg_color = 0x000000;
    }
    return row;
}

void runScrollbackBenchmarks(BenchmarkRunner& runner) {
    constexpr int kCols = 80;

    // --- pushRow throughput (10000 max rows) ---
    {
        runner.run("scrollback_push_throughput", "Kops/s", [&]() -> double {
            termcore::ScrollbackRing ring(kCols, 10000);
            constexpr int kPushCount = 10000;

            // Pre-build a row to isolate push cost
            auto row = makeRow(kCols, 42);

            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < kPushCount; ++i) {
                ring.pushRow(row);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(kPushCount) / 1000.0) / sec;
        });
    }

    // --- Sequential cellAt access ---
    {
        runner.run("scrollback_cellAt_sequential", "Kops/s", [&]() -> double {
            termcore::ScrollbackRing ring(kCols, 5000);

            // Fill with 5000 rows
            auto row = makeRow(kCols, 7);
            for (int i = 0; i < 5000; ++i) {
                ring.pushRow(row);
            }

            constexpr int kAccessCount = 5000 * 10; // 10 cols per row sampled
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < 5000; ++i) {
                for (int c = 0; c < kCols; c += 8) { // sample every 8th col
                    volatile auto cell = ring.cellAt(i, c);
                    (void)cell;
                }
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(kAccessCount) / 1000.0) / sec;
        });
    }

    // --- Random cellAt access ---
    {
        runner.run("scrollback_cellAt_random", "Kops/s", [&]() -> double {
            termcore::ScrollbackRing ring(kCols, 5000);

            auto row = makeRow(kCols, 13);
            for (int i = 0; i < 5000; ++i) {
                ring.pushRow(row);
            }

            // Pre-generate random indices to avoid RNG in timed section
            std::mt19937 rng(12345);
            std::uniform_int_distribution<int> row_dist(0, 4999);
            std::uniform_int_distribution<int> col_dist(0, kCols - 1);
            constexpr int kAccessCount = 50000;
            std::vector<std::pair<int, int>> indices(kAccessCount);
            for (auto& idx : indices) {
                idx = {row_dist(rng), col_dist(rng)};
            }

            BenchmarkTimer t;
            t.start();
            for (const auto& [r, c] : indices) {
                volatile auto cell = ring.cellAt(r, c);
                (void)cell;
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(kAccessCount) / 1000.0) / sec;
        });
    }

    // --- Eviction cost (push when at capacity) ---
    {
        runner.run("scrollback_eviction_cost", "Kops/s", [&]() -> double {
            constexpr int kMaxRows = 10000;
            termcore::ScrollbackRing ring(kCols, kMaxRows);

            // Fill to capacity first
            auto row = makeRow(kCols, 99);
            for (int i = 0; i < kMaxRows; ++i) {
                ring.pushRow(row);
            }

            // Now every push triggers eviction
            constexpr int kEvictionPushes = 10000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < kEvictionPushes; ++i) {
                ring.pushRow(row);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(kEvictionPushes) / 1000.0) / sec;
        });
    }

    // --- Large capacity push throughput (100000 max rows) ---
    {
        runner.run("scrollback_large_capacity", "Kops/s", [&]() -> double {
            termcore::ScrollbackRing ring(kCols, 100000);
            constexpr int kPushCount = 50000; // push half capacity to keep memory reasonable

            auto row = makeRow(kCols, 55);

            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < kPushCount; ++i) {
                ring.pushRow(row);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(kPushCount) / 1000.0) / sec;
        });
    }
}

} // namespace bench
