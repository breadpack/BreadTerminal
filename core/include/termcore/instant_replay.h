#ifndef TERMCORE_INSTANT_REPLAY_H
#define TERMCORE_INSTANT_REPLAY_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

/// A snapshot of the terminal screen at a point in time
struct ReplayFrame {
    std::chrono::steady_clock::time_point timestamp;
    int rows = 0;
    int cols = 0;
    int cursorRow = 0;
    int cursorCol = 0;
    /// Flat array of cell data: each cell is codepoint(uint32_t) + fg(uint32_t) + bg(uint32_t) + attrs(uint32_t)
    /// Total size = rows * cols * 4 uint32_t values
    std::vector<uint32_t> cellData;
};

/// Instant Replay - records terminal frames for playback
class InstantReplay {
public:
    InstantReplay();

    /// Configuration
    void setMaxFrames(size_t max);
    size_t maxFrames() const;
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /// Recording
    void captureFrame(int rows, int cols, int cursorRow, int cursorCol,
                      const std::vector<uint32_t>& cellData);

    /// Playback
    bool isReplaying() const;
    void enterReplay();
    void exitReplay();

    /// Navigation
    const ReplayFrame* currentFrame() const;
    bool seekForward();   // returns false if at end
    bool seekBackward();  // returns false if at beginning
    bool seekToStart();
    bool seekToEnd();
    /// Seek by time offset (seconds from current position)
    bool seekByTime(double seconds);

    /// Info
    size_t frameCount() const;
    size_t currentIndex() const;
    /// Time range in seconds
    double totalDuration() const;
    double currentTime() const;  // seconds from start

    /// Clear all frames
    void clear();

private:
    bool enabled_ = true;
    bool replaying_ = false;
    size_t maxFrames_ = 1000;
    size_t currentIndex_ = 0;
    std::vector<ReplayFrame> frames_;
};

} // namespace termcore

#endif // TERMCORE_INSTANT_REPLAY_H
