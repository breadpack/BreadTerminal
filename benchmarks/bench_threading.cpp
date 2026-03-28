#include "bench_threading.h"
#include "termcore/font/raster_queue.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

// ---------------------------------------------------------------------------
// Minimal copy of InputRingBuffer (same as in test_input_ring_buffer.cpp)
// to avoid pulling in platform-specific headers.
// ---------------------------------------------------------------------------
namespace bench_threading {

using WPARAM = uintptr_t;

struct InputEvent {
    enum Type : uint8_t { KeyDown, Char };
    Type type;
    WPARAM wParam;
    uint8_t mods;
};

struct InputRingBuffer {
    static constexpr size_t kCapacity = 256;  // must be power of 2

    bool push(const InputEvent& ev) {
        size_t w = write_.load(std::memory_order_relaxed);
        size_t next = (w + 1) & (kCapacity - 1);
        if (next == read_.load(std::memory_order_acquire)) return false;
        buf_[w] = ev;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(InputEvent& ev) {
        size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        ev = buf_[r];
        read_.store((r + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return read_.load(std::memory_order_acquire) == write_.load(std::memory_order_acquire);
    }

private:
    std::array<InputEvent, kCapacity> buf_{};
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};
};

} // namespace bench_threading

namespace bench {

using bench_threading::InputEvent;
using bench_threading::InputRingBuffer;

void runThreadingBenchmarks(BenchmarkRunner& runner) {

    // ===== InputRingBuffer Benchmarks =====

    // --- Single-threaded push+pop throughput ---
    {
        runner.run("ringbuffer_push_pop_throughput", "Mops/s", [&]() -> double {
            InputRingBuffer rb;
            InputEvent ev{InputEvent::KeyDown, 42, 0};
            InputEvent out{};

            constexpr int kOps = 1000000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < kOps; ++i) {
                rb.push(ev);
                rb.pop(out);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(kOps) / 1e6) / sec;
        });
    }

    // --- SPSC two-thread throughput ---
    {
        runner.run("ringbuffer_spsc_throughput", "Mops/s", [&]() -> double {
            InputRingBuffer rb;
            constexpr int kOps = 500000;
            std::atomic<bool> producer_done{false};
            std::atomic<int> consumed{0};

            BenchmarkTimer t;
            t.start();

            // Producer thread
            std::thread producer([&]() {
                InputEvent ev{InputEvent::KeyDown, 0, 0};
                for (int i = 0; i < kOps; ++i) {
                    ev.wParam = static_cast<uintptr_t>(i);
                    while (!rb.push(ev)) {
                        // Spin — buffer full
                    }
                }
                producer_done.store(true, std::memory_order_release);
            });

            // Consumer (this thread)
            int count = 0;
            InputEvent out{};
            while (count < kOps) {
                if (rb.pop(out)) {
                    ++count;
                }
            }
            consumed.store(count);

            producer.join();
            double sec = t.elapsedSec();
            return (static_cast<double>(kOps) / 1e6) / sec;
        });
    }

    // ===== RasterQueue Benchmarks =====

    // --- Submit throughput (enqueue without contention, no worker) ---
    {
        runner.run("rasterqueue_submit_throughput", "Kops/s", [&]() -> double {
            termcore::RasterQueue queue;

            // Don't start the worker — measure pure enqueue cost.
            // Each request must have a unique key (duplicates are ignored).
            constexpr int kOps = 10000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < kOps; ++i) {
                termcore::RasterRequest req;
                req.key.face_id = 1;
                req.key.glyph_index = static_cast<uint32_t>(i);
                req.key.subpixel = {0, 0};
                req.font_size = 14.0f;
                queue.enqueue(req);
            }
            double sec = t.elapsedSec();
            return (static_cast<double>(kOps) / 1000.0) / sec;
        });
    }

    // --- Roundtrip latency: enqueue -> worker rasterizes -> drain ---
    {
        runner.runTimed("rasterqueue_roundtrip_latency", "us", [&]() {
            termcore::RasterQueue queue;

            // Trivial rasterize function — returns an empty glyph immediately
            queue.start([](termcore::FontFaceId, uint32_t, float,
                           termcore::SubpixelOffset) -> termcore::RasterizedGlyph {
                return termcore::RasterizedGlyph{};
            });

            // Submit a batch and wait for results
            constexpr int kBatchSize = 100;
            for (int i = 0; i < kBatchSize; ++i) {
                termcore::RasterRequest req;
                req.key.face_id = 1;
                req.key.glyph_index = static_cast<uint32_t>(i);
                req.key.subpixel = {0, 0};
                req.font_size = 14.0f;
                queue.enqueue(req);
            }

            // Drain until we have all results (with timeout to prevent hang)
            int total_drained = 0;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (total_drained < kBatchSize) {
                auto results = queue.drainResults();
                total_drained += static_cast<int>(results.size());
                if (total_drained < kBatchSize) {
                    if (std::chrono::steady_clock::now() > deadline) break;
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }

            queue.stop();
        });
    }
}

} // namespace bench
