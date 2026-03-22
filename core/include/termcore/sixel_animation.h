#pragma once
#include <vector>
#include <cstdint>
#include <chrono>

namespace termcore {

/// A single frame of a Sixel animation
struct SixelFrame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // RGBA pixel data
    int delayMs = 100;          // frame delay in milliseconds
};

/// Sixel animation - manages multi-frame Sixel sequences
class SixelAnimation {
public:
    SixelAnimation();

    /// Add a frame
    void addFrame(SixelFrame frame);

    /// Frame access
    size_t frameCount() const;
    const SixelFrame* frame(size_t index) const;
    const SixelFrame* currentFrame() const;

    /// Playback control
    void play();
    void pause();
    void stop();  // resets to frame 0
    bool isPlaying() const;

    /// Set loop mode
    void setLooping(bool loop);
    bool isLooping() const;

    /// Advance animation - call this each render tick
    /// Returns true if frame changed (needs re-render)
    bool tick();

    /// Current frame index
    size_t currentIndex() const;

    /// Set playback speed multiplier (1.0 = normal)
    void setSpeed(float speed);
    float speed() const;

    /// Total duration in milliseconds
    int totalDurationMs() const;

    /// Clear all frames
    void clear();

private:
    std::vector<SixelFrame> frames_;
    size_t currentIndex_ = 0;
    bool playing_ = false;
    bool looping_ = true;
    float speed_ = 1.0f;
    std::chrono::steady_clock::time_point lastFrameTime_;
};

/// Detect if incoming Sixel data is part of an animation sequence
/// (multiple Sixel images sent to the same cursor position)
struct SixelAnimationDetector {
    /// Feed a decoded Sixel image position and check if it's a new frame
    /// at the same position as previous frames
    bool feedImage(int cursorRow, int cursorCol, int width, int height);

    /// Reset detection state
    void reset();

    /// Is current sequence likely an animation?
    bool isAnimation() const;

    /// Frame count in current sequence
    int sequenceFrameCount() const;

private:
    int lastRow_ = -1;
    int lastCol_ = -1;
    int lastWidth_ = 0;
    int lastHeight_ = 0;
    int frameCount_ = 0;
    std::chrono::steady_clock::time_point lastImageTime_;
};

} // namespace termcore
