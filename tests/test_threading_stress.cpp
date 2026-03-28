// Threading stress tests for BreadTerminal's concurrent components:
// InputRingBuffer (SPSC lock-free queue), RasterQueue, and general patterns.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "termcore/font/raster_queue.h"

// ---------------------------------------------------------------------------
// Timeout helper — wraps a test body so it fails instead of hanging.
// ---------------------------------------------------------------------------
#define WITH_TIMEOUT(seconds, body)                                           \
    do {                                                                      \
        std::atomic<bool> done_{false};                                       \
        std::thread timeout_thread_([&] {                                     \
            for (int i_ = 0; i_ < (seconds) * 100 && !done_.load(); ++i_)    \
                std::this_thread::sleep_for(std::chrono::milliseconds(10));   \
            if (!done_.load()) {                                              \
                ADD_FAILURE() << "Test timed out after " << (seconds) << "s"; \
                std::abort();                                                 \
            }                                                                 \
        });                                                                   \
        body;                                                                 \
        done_.store(true);                                                    \
        timeout_thread_.join();                                               \
    } while (0)

// ===========================================================================
// InputRingBuffer — standalone copy for cross-platform testing
// (mirrors platform/windows/include/TerminalWindowState.h)
// ===========================================================================

namespace threading_test {

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
        return read_.load(std::memory_order_acquire) ==
               write_.load(std::memory_order_acquire);
    }

private:
    std::array<InputEvent, kCapacity> buf_{};
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};
};

}  // namespace threading_test

using threading_test::InputEvent;
using threading_test::InputRingBuffer;

static InputEvent makeEvent(InputEvent::Type type, uintptr_t key,
                            uint8_t mods = 0) {
    return InputEvent{type, key, mods};
}

// ===========================================================================
// InputRingBuffer SPSC Concurrent Tests
// ===========================================================================

TEST(ThreadingStress_SPSC, ProducerConsumer) {
    WITH_TIMEOUT(5, {
        InputRingBuffer rb;
        constexpr int kTotal = 10000;
        std::atomic<int> consumed{0};
        std::vector<uintptr_t> received;
        received.reserve(kTotal);

        std::thread producer([&] {
            for (int i = 0; i < kTotal; ++i) {
                auto ev = makeEvent(InputEvent::KeyDown,
                                    static_cast<uintptr_t>(i));
                // Spin until space is available (back-pressure)
                while (!rb.push(ev)) {
                    std::this_thread::yield();
                }
            }
        });

        std::thread consumer([&] {
            InputEvent ev{};
            while (consumed.load(std::memory_order_relaxed) < kTotal) {
                if (rb.pop(ev)) {
                    received.push_back(ev.wParam);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });

        producer.join();
        consumer.join();

        // Verify count
        ASSERT_EQ(static_cast<int>(received.size()), kTotal);

        // Verify ordering — SPSC guarantees FIFO
        for (int i = 0; i < kTotal; ++i) {
            EXPECT_EQ(received[i], static_cast<uintptr_t>(i))
                << "Mismatch at index " << i;
        }
    });
}

TEST(ThreadingStress_SPSC, BurstPattern) {
    WITH_TIMEOUT(5, {
        InputRingBuffer rb;
        constexpr int kBursts = 200;
        constexpr int kBurstSize = 50;
        constexpr int kTotal = kBursts * kBurstSize;
        std::atomic<int> consumed{0};
        std::vector<uintptr_t> received;
        received.reserve(kTotal);

        std::thread producer([&] {
            for (int burst = 0; burst < kBursts; ++burst) {
                for (int i = 0; i < kBurstSize; ++i) {
                    uintptr_t val =
                        static_cast<uintptr_t>(burst * kBurstSize + i);
                    while (!rb.push(makeEvent(InputEvent::KeyDown, val))) {
                        std::this_thread::yield();
                    }
                }
                // Small pause between bursts to simulate real input
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });

        std::thread consumer([&] {
            InputEvent ev{};
            while (consumed.load(std::memory_order_relaxed) < kTotal) {
                // Drain periodically in small batches
                int drained = 0;
                while (drained < kBurstSize && rb.pop(ev)) {
                    received.push_back(ev.wParam);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                    ++drained;
                }
                if (drained == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            }
        });

        producer.join();
        consumer.join();

        ASSERT_EQ(static_cast<int>(received.size()), kTotal);

        // Verify strict FIFO ordering
        for (int i = 0; i < kTotal; ++i) {
            EXPECT_EQ(received[i], static_cast<uintptr_t>(i))
                << "Order violation at index " << i;
        }
    });
}

TEST(ThreadingStress_SPSC, HighContention) {
    WITH_TIMEOUT(5, {
        InputRingBuffer rb;
        constexpr int kTotal = 50000;
        std::atomic<int> produced{0};
        std::atomic<int> consumed{0};

        // Both threads run as fast as possible — no yields, no sleeps
        std::thread producer([&] {
            int i = 0;
            while (i < kTotal) {
                if (rb.push(makeEvent(InputEvent::Char,
                                      static_cast<uintptr_t>(i)))) {
                    ++i;
                    produced.fetch_add(1, std::memory_order_relaxed);
                }
                // Tight spin — intentionally no yield
            }
        });

        std::vector<uintptr_t> received;
        received.reserve(kTotal);

        std::thread consumer([&] {
            InputEvent ev{};
            while (consumed.load(std::memory_order_relaxed) < kTotal) {
                if (rb.pop(ev)) {
                    received.push_back(ev.wParam);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });

        producer.join();
        consumer.join();

        ASSERT_EQ(static_cast<int>(received.size()), kTotal);

        // Verify data integrity
        for (int i = 0; i < kTotal; ++i) {
            EXPECT_EQ(received[i], static_cast<uintptr_t>(i))
                << "Data corruption at index " << i;
        }
    });
}

TEST(ThreadingStress_SPSC, CapacityPressure) {
    WITH_TIMEOUT(30, {
        InputRingBuffer rb;
        constexpr int kTotal = 2000;
        std::atomic<int> consumed{0};
        std::atomic<int> push_failures{0};

        // Producer is much faster than consumer
        std::thread producer([&] {
            int i = 0;
            while (i < kTotal) {
                if (rb.push(makeEvent(InputEvent::KeyDown,
                                      static_cast<uintptr_t>(i)))) {
                    ++i;
                } else {
                    push_failures.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                }
            }
        });

        std::vector<uintptr_t> received;
        received.reserve(kTotal);

        std::thread consumer([&] {
            InputEvent ev{};
            while (consumed.load(std::memory_order_relaxed) < kTotal) {
                if (rb.pop(ev)) {
                    received.push_back(ev.wParam);
                    consumed.fetch_add(1, std::memory_order_relaxed);
                    // Simulate slow consumer
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                } else {
                    std::this_thread::yield();
                }
            }
        });

        producer.join();
        consumer.join();

        // Should have experienced back-pressure (push failures)
        EXPECT_GT(push_failures.load(), 0)
            << "Expected some push failures under capacity pressure";

        // All events must still arrive in order
        ASSERT_EQ(static_cast<int>(received.size()), kTotal);
        for (int i = 0; i < kTotal; ++i) {
            EXPECT_EQ(received[i], static_cast<uintptr_t>(i))
                << "Order violation at index " << i;
        }
    });
}

// ===========================================================================
// RasterQueue Concurrent Tests
// ===========================================================================

using namespace termcore;

// Helper to create a GlyphKey with distinct values
static GlyphKey makeGlyphKey(uint32_t id) {
    return GlyphKey{
        /*face_id=*/1,
        /*glyph_index=*/id,
        /*subpixel=*/{0, 0}};
}

// Mock rasterizer that returns a small bitmap and tracks call count
static RasterizedGlyph mockRasterize(std::atomic<int>& callCount,
                                     FontFaceId /*face*/, uint32_t glyph_index,
                                     float /*size*/, SubpixelOffset /*sp*/) {
    callCount.fetch_add(1, std::memory_order_relaxed);
    RasterizedGlyph g;
    g.width = 8;
    g.height = 16;
    g.bearing_x = 0;
    g.bearing_y = 14;
    g.format = PixelFormat::Grayscale;
    g.bitmap.resize(8 * 16, static_cast<uint8_t>(glyph_index & 0xFF));
    return g;
}

TEST(ThreadingStress_RasterQueue, ProducerConsumerThroughput) {
    WITH_TIMEOUT(5, {
        RasterQueue queue;
        std::atomic<int> rasterCount{0};

        queue.start([&](FontFaceId f, uint32_t gi, float s, SubpixelOffset sp) {
            return mockRasterize(rasterCount, f, gi, s, sp);
        });

        constexpr int kRequests = 1000;

        // Submit all requests from "main thread"
        for (int i = 0; i < kRequests; ++i) {
            queue.enqueue(RasterRequest{makeGlyphKey(static_cast<uint32_t>(i)),
                                        14.0f});
        }

        // Drain results until we have them all
        std::vector<RasterResult> all_results;
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(4);

        while (static_cast<int>(all_results.size()) < kRequests &&
               std::chrono::steady_clock::now() < deadline) {
            auto batch = queue.drainResults();
            all_results.insert(all_results.end(),
                               std::make_move_iterator(batch.begin()),
                               std::make_move_iterator(batch.end()));
            if (static_cast<int>(all_results.size()) < kRequests) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        queue.stop();

        ASSERT_EQ(static_cast<int>(all_results.size()), kRequests);
        EXPECT_EQ(rasterCount.load(), kRequests);

        // Verify each result has valid bitmap data
        for (auto& r : all_results) {
            EXPECT_EQ(r.glyph.width, 8);
            EXPECT_EQ(r.glyph.height, 16);
            EXPECT_EQ(static_cast<int>(r.glyph.bitmap.size()), 8 * 16);
        }
    });
}

TEST(ThreadingStress_RasterQueue, DuplicateRequestDedup) {
    WITH_TIMEOUT(5, {
        RasterQueue queue;
        std::atomic<int> rasterCount{0};

        // Slow rasterizer so duplicates arrive while first is still pending
        queue.start([&](FontFaceId f, uint32_t gi, float s,
                        SubpixelOffset sp) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return mockRasterize(rasterCount, f, gi, s, sp);
        });

        // Submit the same glyph key 50 times
        auto key = makeGlyphKey(42);
        for (int i = 0; i < 50; ++i) {
            queue.enqueue(RasterRequest{key, 14.0f});
        }

        // Wait for processing
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(3);
        std::vector<RasterResult> all_results;

        while (std::chrono::steady_clock::now() < deadline) {
            auto batch = queue.drainResults();
            all_results.insert(all_results.end(),
                               std::make_move_iterator(batch.begin()),
                               std::make_move_iterator(batch.end()));
            if (!all_results.empty() && !queue.hasPending()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        queue.stop();

        // Should have rasterized only once due to dedup
        EXPECT_EQ(rasterCount.load(), 1)
            << "Duplicate requests were not deduplicated";
        ASSERT_EQ(static_cast<int>(all_results.size()), 1);
        EXPECT_EQ(all_results[0].key.glyph_index, 42u);
    });
}

TEST(ThreadingStress_RasterQueue, StopDuringProcessing) {
    WITH_TIMEOUT(5, {
        RasterQueue queue;
        std::atomic<int> rasterCount{0};
        std::atomic<bool> workerBusy{false};

        queue.start([&](FontFaceId f, uint32_t gi, float s,
                        SubpixelOffset sp) {
            workerBusy.store(true, std::memory_order_release);
            // Simulate slow rasterization
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return mockRasterize(rasterCount, f, gi, s, sp);
        });

        // Enqueue several requests
        for (int i = 0; i < 100; ++i) {
            queue.enqueue(RasterRequest{makeGlyphKey(static_cast<uint32_t>(i)),
                                        14.0f});
        }

        // Wait until worker is busy
        while (!workerBusy.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        // Stop while worker is processing — must not deadlock or crash
        queue.stop();

        // The worker should have been joined cleanly. Not all 100 need to
        // complete, but it must not crash.
        EXPECT_GE(rasterCount.load(), 1)
            << "Worker should have processed at least one request";
        SUCCEED() << "Clean shutdown during processing";
    });
}

TEST(ThreadingStress_RasterQueue, RapidSubmitAndDrain) {
    WITH_TIMEOUT(5, {
        RasterQueue queue;
        std::atomic<int> rasterCount{0};

        queue.start([&](FontFaceId f, uint32_t gi, float s, SubpixelOffset sp) {
            return mockRasterize(rasterCount, f, gi, s, sp);
        });

        constexpr int kCycles = 100;
        constexpr int kPerCycle = 10;
        int totalDrained = 0;

        for (int cycle = 0; cycle < kCycles; ++cycle) {
            // Submit a small batch
            for (int i = 0; i < kPerCycle; ++i) {
                uint32_t id = static_cast<uint32_t>(cycle * kPerCycle + i);
                queue.enqueue(RasterRequest{makeGlyphKey(id), 14.0f});
            }

            // Drain whatever is ready (may be 0)
            auto batch = queue.drainResults();
            totalDrained += static_cast<int>(batch.size());
        }

        // Drain remaining
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (totalDrained < kCycles * kPerCycle &&
               std::chrono::steady_clock::now() < deadline) {
            auto batch = queue.drainResults();
            totalDrained += static_cast<int>(batch.size());
            if (totalDrained < kCycles * kPerCycle) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        queue.stop();

        EXPECT_EQ(totalDrained, kCycles * kPerCycle)
            << "Not all results were drained";
        EXPECT_EQ(rasterCount.load(), kCycles * kPerCycle);
    });
}

// ===========================================================================
// General Thread Safety Pattern Tests
// ===========================================================================

TEST(ThreadingStress_General, AtomicBoolShutdown) {
    WITH_TIMEOUT(5, {
        // Verify the atomic<bool> shutdown pattern used across the codebase
        std::atomic<bool> running{true};
        std::atomic<int> iterations{0};

        std::thread worker([&] {
            while (running.load(std::memory_order_acquire)) {
                iterations.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });

        // Let worker run for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Signal shutdown
        running.store(false, std::memory_order_release);
        worker.join();

        // Worker must have run at least once and stopped
        EXPECT_GT(iterations.load(), 0);

        // Verify idempotent shutdown: storing false again is safe
        running.store(false, std::memory_order_release);
        SUCCEED() << "Atomic bool shutdown pattern works correctly";
    });
}

TEST(ThreadingStress_General, ConditionVariableWakeup) {
    WITH_TIMEOUT(5, {
        std::mutex mtx;
        std::condition_variable cv;
        bool ready = false;
        std::atomic<bool> woke_up{false};

        auto start_time = std::chrono::steady_clock::now();

        std::thread waiter([&] {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&] { return ready; });
            woke_up.store(true, std::memory_order_release);
        });

        // Small delay to ensure waiter is blocked
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        EXPECT_FALSE(woke_up.load(std::memory_order_acquire))
            << "Waiter woke up before notification";

        // Notify
        {
            std::lock_guard<std::mutex> lock(mtx);
            ready = true;
        }
        cv.notify_one();

        waiter.join();
        auto elapsed = std::chrono::steady_clock::now() - start_time;

        EXPECT_TRUE(woke_up.load(std::memory_order_acquire));

        // Should wake up promptly — well under 1 second
        auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                .count();
        EXPECT_LT(ms, 1000) << "Condition variable wakeup took " << ms
                             << "ms — too slow";
    });
}
