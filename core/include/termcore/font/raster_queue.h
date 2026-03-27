#ifndef TERMCORE_RASTER_QUEUE_H
#define TERMCORE_RASTER_QUEUE_H

#include "termcore/font/font_metrics.h"
#include "termcore/font/i_font_rasterizer.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>
#include <vector>

namespace termcore {

struct RasterRequest {
    GlyphKey key;
    float font_size;
};

struct RasterResult {
    GlyphKey key;
    RasterizedGlyph glyph;
};

/// Producer-consumer queue for background glyph rasterization.
/// The render thread enqueues requests on cache miss and drains results each frame.
/// A single worker thread picks up requests and rasterizes glyphs asynchronously.
class RasterQueue {
public:
    RasterQueue();
    ~RasterQueue();

    // Non-copyable, non-movable
    RasterQueue(const RasterQueue&) = delete;
    RasterQueue& operator=(const RasterQueue&) = delete;

    /// Rasterization callback type — called on the worker thread.
    /// Parameters: face_id, glyph_index, font_size, subpixel_offset
    using RasterizeFn = std::function<RasterizedGlyph(FontFaceId, uint32_t, float, SubpixelOffset)>;

    /// Start the worker thread with the given rasterization function.
    void start(RasterizeFn fn);

    /// Stop the worker thread and drain remaining work.
    void stop();

    /// Producer (render thread): submit a raster request.
    /// Duplicate requests (already pending) are silently ignored.
    void enqueue(const RasterRequest& request);

    /// Consumer (render thread): drain all completed results.
    std::vector<RasterResult> drainResults();

    /// Check if there are pending requests that haven't completed yet.
    bool hasPending() const;

private:
    void workerLoop();

    // Request queue (render thread -> worker thread)
    std::mutex request_mutex_;
    std::condition_variable request_cv_;
    std::queue<RasterRequest> requests_;

    // Track in-flight keys to avoid duplicate enqueues
    std::unordered_set<GlyphKey> pending_keys_;

    // Result queue (worker thread -> render thread)
    std::mutex result_mutex_;
    std::vector<RasterResult> results_;

    // Worker thread
    std::thread worker_;
    std::atomic<bool> running_{false};

    RasterizeFn rasterize_fn_;
};

} // namespace termcore

#endif
