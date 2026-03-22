#include "termcore/sixel_animation.h"
#include <algorithm>

namespace termcore {

// ---------------------------------------------------------------------------
// SixelAnimation
// ---------------------------------------------------------------------------

SixelAnimation::SixelAnimation()
    : lastFrameTime_(std::chrono::steady_clock::now()) {}

void SixelAnimation::addFrame(SixelFrame frame) {
    frames_.push_back(std::move(frame));
}

size_t SixelAnimation::frameCount() const {
    return frames_.size();
}

const SixelFrame* SixelAnimation::frame(size_t index) const {
    if (index >= frames_.size()) return nullptr;
    return &frames_[index];
}

const SixelFrame* SixelAnimation::currentFrame() const {
    return frame(currentIndex_);
}

void SixelAnimation::play() {
    if (frames_.empty()) return;
    playing_ = true;
    lastFrameTime_ = std::chrono::steady_clock::now();
}

void SixelAnimation::pause() {
    playing_ = false;
}

void SixelAnimation::stop() {
    playing_ = false;
    currentIndex_ = 0;
}

bool SixelAnimation::isPlaying() const {
    return playing_;
}

void SixelAnimation::setLooping(bool loop) {
    looping_ = loop;
}

bool SixelAnimation::isLooping() const {
    return looping_;
}

bool SixelAnimation::tick() {
    if (!playing_ || frames_.empty()) return false;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastFrameTime_).count();

    const auto& current = frames_[currentIndex_];
    int effectiveDelay = static_cast<int>(
        static_cast<float>(current.delayMs) / std::max(speed_, 0.001f));
    if (effectiveDelay < 1) effectiveDelay = 1;

    if (elapsed < effectiveDelay) return false;

    // Time to advance
    lastFrameTime_ = now;

    if (currentIndex_ + 1 < frames_.size()) {
        ++currentIndex_;
    } else if (looping_) {
        currentIndex_ = 0;
    } else {
        playing_ = false;
        return false;
    }

    return true;
}

size_t SixelAnimation::currentIndex() const {
    return currentIndex_;
}

void SixelAnimation::setSpeed(float s) {
    speed_ = std::max(s, 0.001f);
}

float SixelAnimation::speed() const {
    return speed_;
}

int SixelAnimation::totalDurationMs() const {
    int total = 0;
    for (const auto& f : frames_) {
        total += f.delayMs;
    }
    return total;
}

void SixelAnimation::clear() {
    frames_.clear();
    currentIndex_ = 0;
    playing_ = false;
}

// ---------------------------------------------------------------------------
// SixelAnimationDetector
// ---------------------------------------------------------------------------

bool SixelAnimationDetector::feedImage(int cursorRow, int cursorCol,
                                       int width, int height) {
    auto now = std::chrono::steady_clock::now();

    if (cursorRow == lastRow_ && cursorCol == lastCol_ &&
        width == lastWidth_ && height == lastHeight_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastImageTime_).count();
        if (elapsed < 1000) {
            ++frameCount_;
            lastImageTime_ = now;
            return true;
        }
    }

    // New sequence
    lastRow_ = cursorRow;
    lastCol_ = cursorCol;
    lastWidth_ = width;
    lastHeight_ = height;
    frameCount_ = 1;
    lastImageTime_ = now;
    return false;
}

void SixelAnimationDetector::reset() {
    lastRow_ = -1;
    lastCol_ = -1;
    lastWidth_ = 0;
    lastHeight_ = 0;
    frameCount_ = 0;
}

bool SixelAnimationDetector::isAnimation() const {
    return frameCount_ >= 2;
}

int SixelAnimationDetector::sequenceFrameCount() const {
    return frameCount_;
}

} // namespace termcore
