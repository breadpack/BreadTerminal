#include "termcore/font/raster_queue.h"

namespace termcore {

RasterQueue::RasterQueue() = default;

RasterQueue::~RasterQueue() {
    stop();
}

void RasterQueue::start(RasterizeFn fn) {
    if (running_.load()) {
        return;  // Already running
    }

    rasterize_fn_ = std::move(fn);
    running_.store(true);
    worker_ = std::thread(&RasterQueue::workerLoop, this);
}

void RasterQueue::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    // Wake up the worker so it can exit
    request_cv_.notify_one();

    if (worker_.joinable()) {
        worker_.join();
    }

    // Clear any remaining requests
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        std::queue<RasterRequest> empty;
        requests_.swap(empty);
        pending_keys_.clear();
    }
}

void RasterQueue::enqueue(const RasterRequest& request) {
    std::lock_guard<std::mutex> lock(request_mutex_);

    // Skip if this key is already pending
    if (pending_keys_.count(request.key)) {
        return;
    }

    pending_keys_.insert(request.key);
    requests_.push(request);
    request_cv_.notify_one();
}

std::vector<RasterResult> RasterQueue::drainResults() {
    std::lock_guard<std::mutex> lock(result_mutex_);
    std::vector<RasterResult> drained;
    drained.swap(results_);
    return drained;
}

bool RasterQueue::hasPending() const {
    // Relaxed check — not perfectly synchronized but good enough for heuristics
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(request_mutex_));
    return !requests_.empty() || !pending_keys_.empty();
}

void RasterQueue::workerLoop() {
    while (running_.load()) {
        RasterRequest request;

        // Wait for a request
        {
            std::unique_lock<std::mutex> lock(request_mutex_);
            request_cv_.wait(lock, [this] {
                return !requests_.empty() || !running_.load();
            });

            if (!running_.load() && requests_.empty()) {
                break;
            }

            if (requests_.empty()) {
                continue;
            }

            request = std::move(requests_.front());
            requests_.pop();
        }

        // Rasterize outside the lock
        RasterizedGlyph glyph = rasterize_fn_(
            request.key.face_id,
            request.key.glyph_index,
            request.font_size,
            request.key.subpixel
        );

        // Push result
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            results_.push_back(RasterResult{request.key, std::move(glyph)});
        }

        // Remove from pending set
        {
            std::lock_guard<std::mutex> lock(request_mutex_);
            pending_keys_.erase(request.key);
        }
    }
}

} // namespace termcore
