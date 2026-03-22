#include <gtest/gtest.h>
#include "termcore/instant_replay.h"

#include <thread>

using namespace termcore;

class InstantReplayTest : public ::testing::Test {
protected:
    InstantReplay replay;

    // Helper: create dummy cell data for rows x cols
    std::vector<uint32_t> makeCellData(int rows, int cols, uint32_t fill = 0) {
        return std::vector<uint32_t>(static_cast<size_t>(rows) * cols * 4, fill);
    }
};

// 1. Initial state
TEST_F(InstantReplayTest, InitialState) {
    EXPECT_TRUE(replay.isEnabled());
    EXPECT_FALSE(replay.isReplaying());
    EXPECT_EQ(replay.frameCount(), 0u);
    EXPECT_EQ(replay.maxFrames(), 1000u);
    EXPECT_DOUBLE_EQ(replay.totalDuration(), 0.0);
}

// 2. Capture frame and retrieve
TEST_F(InstantReplayTest, CaptureAndRetrieve) {
    auto data = makeCellData(24, 80, 'A');
    replay.captureFrame(24, 80, 0, 0, data);
    EXPECT_EQ(replay.frameCount(), 1u);

    replay.enterReplay();
    ASSERT_NE(replay.currentFrame(), nullptr);
    EXPECT_EQ(replay.currentFrame()->rows, 24);
    EXPECT_EQ(replay.currentFrame()->cols, 80);
    EXPECT_EQ(replay.currentFrame()->cellData, data);
}

// 3. Max frames ring buffer
TEST_F(InstantReplayTest, MaxFramesEviction) {
    replay.setMaxFrames(3);
    EXPECT_EQ(replay.maxFrames(), 3u);

    for (int i = 0; i < 5; ++i) {
        auto data = makeCellData(1, 1, static_cast<uint32_t>(i));
        replay.captureFrame(1, 1, 0, 0, data);
    }

    EXPECT_EQ(replay.frameCount(), 3u);

    // The oldest frames (0 and 1) should have been evicted; first frame has fill=2
    replay.enterReplay();
    replay.seekToStart();
    ASSERT_NE(replay.currentFrame(), nullptr);
    EXPECT_EQ(replay.currentFrame()->cellData[0], 2u);
}

// 4. Enter and exit replay
TEST_F(InstantReplayTest, EnterExitReplay) {
    auto data = makeCellData(1, 1);
    replay.captureFrame(1, 1, 0, 0, data);

    EXPECT_FALSE(replay.isReplaying());
    replay.enterReplay();
    EXPECT_TRUE(replay.isReplaying());
    replay.exitReplay();
    EXPECT_FALSE(replay.isReplaying());
}

// 5. Enter replay with no frames does nothing
TEST_F(InstantReplayTest, EnterReplayEmpty) {
    replay.enterReplay();
    EXPECT_FALSE(replay.isReplaying());
    EXPECT_EQ(replay.currentFrame(), nullptr);
}

// 6. Navigate forward and backward
TEST_F(InstantReplayTest, NavigateForwardBackward) {
    for (int i = 0; i < 5; ++i) {
        auto data = makeCellData(1, 1, static_cast<uint32_t>(i));
        replay.captureFrame(1, 1, 0, 0, data);
    }

    replay.enterReplay();
    // Starts at last frame (index 4)
    EXPECT_EQ(replay.currentIndex(), 4u);
    EXPECT_EQ(replay.currentFrame()->cellData[0], 4u);

    // Seek backward
    EXPECT_TRUE(replay.seekBackward());
    EXPECT_EQ(replay.currentIndex(), 3u);
    EXPECT_EQ(replay.currentFrame()->cellData[0], 3u);

    // Seek forward
    EXPECT_TRUE(replay.seekForward());
    EXPECT_EQ(replay.currentIndex(), 4u);

    // Cannot seek forward past end
    EXPECT_FALSE(replay.seekForward());
    EXPECT_EQ(replay.currentIndex(), 4u);
}

// 7. Seek to start and end
TEST_F(InstantReplayTest, SeekToStartEnd) {
    for (int i = 0; i < 5; ++i) {
        auto data = makeCellData(1, 1, static_cast<uint32_t>(i));
        replay.captureFrame(1, 1, 0, 0, data);
    }

    replay.enterReplay();
    EXPECT_TRUE(replay.seekToStart());
    EXPECT_EQ(replay.currentIndex(), 0u);
    EXPECT_EQ(replay.currentFrame()->cellData[0], 0u);

    // Already at start
    EXPECT_FALSE(replay.seekToStart());

    EXPECT_TRUE(replay.seekToEnd());
    EXPECT_EQ(replay.currentIndex(), 4u);

    // Already at end
    EXPECT_FALSE(replay.seekToEnd());
}

// 8. Cannot seek backward past beginning
TEST_F(InstantReplayTest, SeekBackwardAtBeginning) {
    auto data = makeCellData(1, 1);
    replay.captureFrame(1, 1, 0, 0, data);

    replay.enterReplay();
    // Only one frame, already at index 0 (which is also the last)
    EXPECT_EQ(replay.currentIndex(), 0u);
    EXPECT_FALSE(replay.seekBackward());
}

// 9. seekByTime
TEST_F(InstantReplayTest, SeekByTime) {
    // Capture frames with small delays to create measurable time differences
    for (int i = 0; i < 5; ++i) {
        auto data = makeCellData(1, 1, static_cast<uint32_t>(i));
        replay.captureFrame(1, 1, 0, 0, data);
        if (i < 4) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    replay.enterReplay();
    // At end. Seek backward by a large amount -> should go to start
    EXPECT_TRUE(replay.seekByTime(-10.0));
    EXPECT_EQ(replay.currentIndex(), 0u);

    // Seek forward by a large amount -> should go to end
    EXPECT_TRUE(replay.seekByTime(10.0));
    EXPECT_EQ(replay.currentIndex(), 4u);

    // Seeking by 0 from endpoint (already clamped) -> false
    EXPECT_FALSE(replay.seekByTime(10.0));
}

// 10. Duration calculations
TEST_F(InstantReplayTest, DurationCalculations) {
    auto data = makeCellData(1, 1);
    replay.captureFrame(1, 1, 0, 0, data);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    replay.captureFrame(1, 1, 0, 0, data);

    double dur = replay.totalDuration();
    EXPECT_GE(dur, 0.04);  // at least ~40ms
    EXPECT_LE(dur, 1.0);   // but not more than 1s

    replay.enterReplay();
    // At last frame, currentTime should equal totalDuration
    double ct = replay.currentTime();
    EXPECT_NEAR(ct, dur, 0.001);

    // At first frame, currentTime should be 0
    replay.seekToStart();
    EXPECT_DOUBLE_EQ(replay.currentTime(), 0.0);
}

// 11. No capture while replaying
TEST_F(InstantReplayTest, NoCaptureWhileReplaying) {
    auto data = makeCellData(1, 1);
    replay.captureFrame(1, 1, 0, 0, data);
    EXPECT_EQ(replay.frameCount(), 1u);

    replay.enterReplay();
    replay.captureFrame(1, 1, 0, 0, data);
    EXPECT_EQ(replay.frameCount(), 1u);  // No new frame added

    replay.exitReplay();
    replay.captureFrame(1, 1, 0, 0, data);
    EXPECT_EQ(replay.frameCount(), 2u);  // Now it works
}

// 12. No capture when disabled
TEST_F(InstantReplayTest, NoCaptureWhenDisabled) {
    replay.setEnabled(false);
    EXPECT_FALSE(replay.isEnabled());

    auto data = makeCellData(1, 1);
    replay.captureFrame(1, 1, 0, 0, data);
    EXPECT_EQ(replay.frameCount(), 0u);

    replay.setEnabled(true);
    replay.captureFrame(1, 1, 0, 0, data);
    EXPECT_EQ(replay.frameCount(), 1u);
}

// 13. Clear
TEST_F(InstantReplayTest, Clear) {
    auto data = makeCellData(1, 1);
    replay.captureFrame(1, 1, 0, 0, data);
    replay.enterReplay();
    EXPECT_TRUE(replay.isReplaying());

    replay.clear();
    EXPECT_EQ(replay.frameCount(), 0u);
    EXPECT_FALSE(replay.isReplaying());
    EXPECT_EQ(replay.currentIndex(), 0u);
}

// 14. setMaxFrames trims existing frames
TEST_F(InstantReplayTest, SetMaxFramesTrims) {
    for (int i = 0; i < 10; ++i) {
        auto data = makeCellData(1, 1, static_cast<uint32_t>(i));
        replay.captureFrame(1, 1, 0, 0, data);
    }
    EXPECT_EQ(replay.frameCount(), 10u);

    replay.setMaxFrames(3);
    EXPECT_EQ(replay.frameCount(), 3u);
}

// 15. Cursor position is preserved
TEST_F(InstantReplayTest, CursorPosition) {
    auto data = makeCellData(24, 80);
    replay.captureFrame(24, 80, 10, 42, data);

    replay.enterReplay();
    ASSERT_NE(replay.currentFrame(), nullptr);
    EXPECT_EQ(replay.currentFrame()->cursorRow, 10);
    EXPECT_EQ(replay.currentFrame()->cursorCol, 42);
}

// 16. currentFrame returns nullptr when not replaying
TEST_F(InstantReplayTest, CurrentFrameNullWhenNotReplaying) {
    auto data = makeCellData(1, 1);
    replay.captureFrame(1, 1, 0, 0, data);
    EXPECT_EQ(replay.currentFrame(), nullptr);
}

// 17. Navigation returns false when not replaying
TEST_F(InstantReplayTest, NavigationFailsWhenNotReplaying) {
    auto data = makeCellData(1, 1);
    replay.captureFrame(1, 1, 0, 0, data);

    EXPECT_FALSE(replay.seekForward());
    EXPECT_FALSE(replay.seekBackward());
    EXPECT_FALSE(replay.seekToStart());
    EXPECT_FALSE(replay.seekToEnd());
    EXPECT_FALSE(replay.seekByTime(1.0));
}
