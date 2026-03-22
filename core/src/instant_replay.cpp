#include "termcore/instant_replay.h"

#include <algorithm>
#include <cmath>

namespace termcore {

InstantReplay::InstantReplay() = default;

void InstantReplay::setMaxFrames(size_t max) {
    maxFrames_ = max;
    // Trim if current buffer exceeds new limit
    while (frames_.size() > maxFrames_) {
        frames_.erase(frames_.begin());
        if (currentIndex_ > 0)
            --currentIndex_;
        else
            currentIndex_ = 0;
    }
}

size_t InstantReplay::maxFrames() const {
    return maxFrames_;
}

void InstantReplay::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool InstantReplay::isEnabled() const {
    return enabled_;
}

void InstantReplay::captureFrame(int rows, int cols, int cursorRow, int cursorCol,
                                  const std::vector<uint32_t>& cellData) {
    if (!enabled_ || replaying_) return;

    ReplayFrame frame;
    frame.timestamp = std::chrono::steady_clock::now();
    frame.rows = rows;
    frame.cols = cols;
    frame.cursorRow = cursorRow;
    frame.cursorCol = cursorCol;
    frame.cellData = cellData;

    frames_.push_back(std::move(frame));

    // Evict oldest frames if over limit
    while (frames_.size() > maxFrames_) {
        frames_.erase(frames_.begin());
    }
}

bool InstantReplay::isReplaying() const {
    return replaying_;
}

void InstantReplay::enterReplay() {
    if (frames_.empty()) return;
    replaying_ = true;
    currentIndex_ = frames_.size() - 1;
}

void InstantReplay::exitReplay() {
    replaying_ = false;
}

const ReplayFrame* InstantReplay::currentFrame() const {
    if (!replaying_ || frames_.empty()) return nullptr;
    if (currentIndex_ >= frames_.size()) return nullptr;
    return &frames_[currentIndex_];
}

bool InstantReplay::seekForward() {
    if (!replaying_ || frames_.empty()) return false;
    if (currentIndex_ >= frames_.size() - 1) return false;
    ++currentIndex_;
    return true;
}

bool InstantReplay::seekBackward() {
    if (!replaying_ || frames_.empty()) return false;
    if (currentIndex_ == 0) return false;
    --currentIndex_;
    return true;
}

bool InstantReplay::seekToStart() {
    if (!replaying_ || frames_.empty()) return false;
    if (currentIndex_ == 0) return false;
    currentIndex_ = 0;
    return true;
}

bool InstantReplay::seekToEnd() {
    if (!replaying_ || frames_.empty()) return false;
    size_t last = frames_.size() - 1;
    if (currentIndex_ == last) return false;
    currentIndex_ = last;
    return true;
}

bool InstantReplay::seekByTime(double seconds) {
    if (!replaying_ || frames_.empty()) return false;

    // Compute target time
    auto currentTs = frames_[currentIndex_].timestamp;
    auto offset = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(seconds));
    auto targetTs = currentTs + offset;

    // Clamp to valid range
    if (targetTs <= frames_.front().timestamp) {
        if (currentIndex_ == 0) return false;
        currentIndex_ = 0;
        return true;
    }
    if (targetTs >= frames_.back().timestamp) {
        size_t last = frames_.size() - 1;
        if (currentIndex_ == last) return false;
        currentIndex_ = last;
        return true;
    }

    // Find closest frame to targetTs
    size_t bestIdx = currentIndex_;
    auto bestDiff = std::chrono::steady_clock::duration::max();

    for (size_t i = 0; i < frames_.size(); ++i) {
        auto diff = frames_[i].timestamp > targetTs
                        ? frames_[i].timestamp - targetTs
                        : targetTs - frames_[i].timestamp;
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIdx = i;
        }
    }

    if (bestIdx == currentIndex_) return false;
    currentIndex_ = bestIdx;
    return true;
}

size_t InstantReplay::frameCount() const {
    return frames_.size();
}

size_t InstantReplay::currentIndex() const {
    return currentIndex_;
}

double InstantReplay::totalDuration() const {
    if (frames_.size() < 2) return 0.0;
    auto diff = frames_.back().timestamp - frames_.front().timestamp;
    return std::chrono::duration<double>(diff).count();
}

double InstantReplay::currentTime() const {
    if (frames_.empty() || !replaying_) return 0.0;
    if (currentIndex_ >= frames_.size()) return 0.0;
    auto diff = frames_[currentIndex_].timestamp - frames_.front().timestamp;
    return std::chrono::duration<double>(diff).count();
}

void InstantReplay::clear() {
    frames_.clear();
    currentIndex_ = 0;
    replaying_ = false;
}

} // namespace termcore
