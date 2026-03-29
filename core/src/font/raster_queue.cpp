#include "termcore/font/raster_queue.h"
#include <chrono>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_mm_pause)
#elif defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#else
// ARM64 and other architectures: provide a spin-wait hint
static inline void _mm_pause() {
#if defined(__aarch64__)
    __asm__ volatile("yield");
#else
    // no-op for unknown architectures
#endif
}
#endif

namespace termcore {

RasterQueue::RasterQueue()
    : result_ring_(kResultRingCapacity) {
    requests_.reserve(256);
}

RasterQueue::~RasterQueue() {
    stop();
}

void RasterQueue::start(RasterizeFn fn) {
    if (running_.load()) {
        return;
    }

    rasterize_fn_ = std::move(fn);
    running_.store(true, std::memory_order_release);
    worker_ = std::thread(&RasterQueue::workerLoop, this);
}

void RasterQueue::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false, std::memory_order_release);
    has_work_.store(true, std::memory_order_release);
    request_cv_.notify_one();

    if (worker_.joinable()) {
        worker_.join();
    }

    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        requests_.clear();
        pending_keys_.clear();
    }

    pending_count_.store(0, std::memory_order_relaxed);
}

void RasterQueue::enqueue(const RasterRequest& request) {
    {
        std::lock_guard<std::mutex> lock(request_mutex_);

        if (pending_keys_.count(request.key)) {
            return;
        }

        pending_keys_.insert(request.key);
        requests_.push_back(request);
        pending_count_.fetch_add(1, std::memory_order_relaxed);
    }

    has_work_.store(true, std::memory_order_release);
    request_cv_.notify_one();
}

std::vector<RasterResult> RasterQueue::drainResults() {
    // Adaptive spin: if items are pending but no results yet, spin briefly
    // to avoid the caller's coarse sleep (Windows rounds sleep_for(10us) to ~15.6ms).
    int pending = pending_count_.load(std::memory_order_acquire);
    if (pending > 0) {
        size_t r = result_read_.load(std::memory_order_relaxed);
        if (r == result_write_.load(std::memory_order_acquire)) {
            // No results yet. Spin with increasing urgency until results or timeout.
            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(2);
            while (std::chrono::steady_clock::now() < deadline) {
                if (r != result_write_.load(std::memory_order_acquire)) {
                    break;
                }
                for (int i = 0; i < 32; ++i) _mm_pause();
            }
        }
        // If results exist, wait briefly for the full batch to land.
        // The worker writes results atomically per-batch (single result_write_ update).
        // A short pause ensures we see the final write.
        size_t r2 = result_read_.load(std::memory_order_relaxed);
        size_t w2 = result_write_.load(std::memory_order_acquire);
        if (r2 != w2) {
            int available = static_cast<int>((w2 - r2) & (kResultRingCapacity - 1));
            if (available < pending) {
                // More results may be incoming. Brief spin.
                for (int i = 0; i < 256; ++i) {
                    size_t w3 = result_write_.load(std::memory_order_acquire);
                    int avail3 = static_cast<int>((w3 - r2) & (kResultRingCapacity - 1));
                    if (avail3 >= pending) break;
                    _mm_pause();
                }
            }
        }
    }

    std::vector<RasterResult> drained;

    size_t r = result_read_.load(std::memory_order_relaxed);
    size_t w = result_write_.load(std::memory_order_acquire);

    if (r == w) {
        return drained;
    }

    size_t count = (w - r) & (kResultRingCapacity - 1);
    drained.reserve(count);

    while (r != w) {
        drained.push_back(std::move(result_ring_[r]));
        r = (r + 1) & (kResultRingCapacity - 1);
    }

    result_read_.store(r, std::memory_order_release);
    return drained;
}

bool RasterQueue::hasPending() const {
    return pending_count_.load(std::memory_order_acquire) > 0;
}

void RasterQueue::workerLoop() {
    std::vector<RasterRequest> local_batch;
    std::vector<RasterResult> local_results;
    local_batch.reserve(256);
    local_results.reserve(256);

    while (running_.load(std::memory_order_acquire)) {
        bool got_work = false;

        // Phase 1: Tight spin with PAUSE on atomic flag
        for (int i = 0; i < 512; ++i) {
            if (has_work_.load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(request_mutex_);
                if (!requests_.empty()) {
                    local_batch.swap(requests_);
                    has_work_.store(false, std::memory_order_relaxed);
                    got_work = true;
                } else {
                    has_work_.store(false, std::memory_order_relaxed);
                }
                break;
            }
            if (!running_.load(std::memory_order_relaxed)) return;
            _mm_pause();
        }

        // Phase 2: Yield spin
        if (!got_work) {
            for (int i = 0; i < 256; ++i) {
                if (has_work_.load(std::memory_order_acquire)) {
                    std::lock_guard<std::mutex> lock(request_mutex_);
                    if (!requests_.empty()) {
                        local_batch.swap(requests_);
                        has_work_.store(false, std::memory_order_relaxed);
                        got_work = true;
                    } else {
                        has_work_.store(false, std::memory_order_relaxed);
                    }
                    break;
                }
                if (!running_.load(std::memory_order_relaxed)) return;
                std::this_thread::yield();
            }
        }

        // Phase 3: Condvar fallback
        if (!got_work) {
            std::unique_lock<std::mutex> lock(request_mutex_);
            request_cv_.wait(lock, [this] {
                return has_work_.load(std::memory_order_relaxed)
                    || !running_.load(std::memory_order_relaxed);
            });

            if (!running_.load(std::memory_order_relaxed) && requests_.empty()) {
                return;
            }

            if (!requests_.empty()) {
                local_batch.swap(requests_);
                has_work_.store(false, std::memory_order_relaxed);
            } else {
                has_work_.store(false, std::memory_order_relaxed);
                continue;
            }
        }

        if (local_batch.empty()) continue;

        // Process entire batch, buffer results locally
        local_results.clear();
        for (auto& request : local_batch) {
            if (!running_.load(std::memory_order_relaxed)) break;

            RasterizedGlyph glyph = rasterize_fn_(
                request.key.face_id,
                request.key.glyph_index,
                request.font_size,
                request.key.subpixel
            );

            local_results.push_back(RasterResult{request.key, std::move(glyph)});
        }

        // Push ALL results into ring buffer, then do a single store-release
        // on result_write_ so the consumer sees the entire batch atomically.
        {
            size_t w = result_write_.load(std::memory_order_relaxed);
            for (auto& result : local_results) {
                size_t next = (w + 1) & (kResultRingCapacity - 1);
                // Spin if ring is full
                while (next == result_read_.load(std::memory_order_acquire)) {
                    if (!running_.load(std::memory_order_relaxed)) goto done;
                    _mm_pause();
                }
                result_ring_[w] = std::move(result);
                w = next;
            }
            // Single atomic publish of all results
            result_write_.store(w, std::memory_order_release);
        }

    done:
        // Remove processed keys from pending set
        {
            std::lock_guard<std::mutex> lock(request_mutex_);
            for (auto& result : local_results) {
                pending_keys_.erase(result.key);
            }
        }

        pending_count_.fetch_sub(static_cast<int>(local_results.size()),
                                 std::memory_order_release);

        local_batch.clear();
    }
}

} // namespace termcore
