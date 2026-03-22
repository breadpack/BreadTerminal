#include <gtest/gtest.h>
#include "termcore/sixel_animation.h"
#include <thread>

using namespace termcore;

// Helper: create a dummy frame with given delay
static SixelFrame makeFrame(int w, int h, int delayMs = 100) {
    SixelFrame f;
    f.width = w;
    f.height = h;
    f.rgba.resize(static_cast<size_t>(w) * h * 4, 0xFF);
    f.delayMs = delayMs;
    return f;
}

// --- SixelAnimation tests ---

TEST(SixelAnimationTest, AddFrameAndCount) {
    SixelAnimation anim;
    EXPECT_EQ(anim.frameCount(), 0u);

    anim.addFrame(makeFrame(10, 10));
    EXPECT_EQ(anim.frameCount(), 1u);

    anim.addFrame(makeFrame(10, 10));
    anim.addFrame(makeFrame(10, 10));
    EXPECT_EQ(anim.frameCount(), 3u);
}

TEST(SixelAnimationTest, FrameAccess) {
    SixelAnimation anim;
    EXPECT_EQ(anim.frame(0), nullptr);
    EXPECT_EQ(anim.currentFrame(), nullptr);

    anim.addFrame(makeFrame(8, 6));
    auto* f = anim.frame(0);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->width, 8);
    EXPECT_EQ(f->height, 6);

    EXPECT_EQ(anim.frame(1), nullptr);
}

TEST(SixelAnimationTest, PlayPauseStopLifecycle) {
    SixelAnimation anim;

    // Play with no frames does nothing
    anim.play();
    EXPECT_FALSE(anim.isPlaying());

    anim.addFrame(makeFrame(4, 4));
    anim.addFrame(makeFrame(4, 4));

    EXPECT_FALSE(anim.isPlaying());
    anim.play();
    EXPECT_TRUE(anim.isPlaying());

    anim.pause();
    EXPECT_FALSE(anim.isPlaying());

    anim.play();
    EXPECT_TRUE(anim.isPlaying());

    anim.stop();
    EXPECT_FALSE(anim.isPlaying());
    EXPECT_EQ(anim.currentIndex(), 0u);
}

TEST(SixelAnimationTest, TickAdvancesFrames) {
    SixelAnimation anim;
    anim.addFrame(makeFrame(2, 2, 10)); // 10ms delay
    anim.addFrame(makeFrame(2, 2, 10));
    anim.addFrame(makeFrame(2, 2, 10));

    anim.play();
    EXPECT_EQ(anim.currentIndex(), 0u);

    // Tick immediately should not advance (not enough time)
    bool changed = anim.tick();
    EXPECT_FALSE(changed);
    EXPECT_EQ(anim.currentIndex(), 0u);

    // Wait and tick
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    changed = anim.tick();
    EXPECT_TRUE(changed);
    EXPECT_EQ(anim.currentIndex(), 1u);
}

TEST(SixelAnimationTest, LoopingWrapsToZero) {
    SixelAnimation anim;
    anim.addFrame(makeFrame(2, 2, 5));
    anim.addFrame(makeFrame(2, 2, 5));
    anim.setLooping(true);
    anim.play();

    // Advance to frame 1
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    anim.tick();
    EXPECT_EQ(anim.currentIndex(), 1u);

    // Advance past last frame -> wraps to 0
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    bool changed = anim.tick();
    EXPECT_TRUE(changed);
    EXPECT_EQ(anim.currentIndex(), 0u);
    EXPECT_TRUE(anim.isPlaying());
}

TEST(SixelAnimationTest, NonLoopingStopsAtEnd) {
    SixelAnimation anim;
    anim.addFrame(makeFrame(2, 2, 5));
    anim.addFrame(makeFrame(2, 2, 5));
    anim.setLooping(false);
    anim.play();

    // Advance to frame 1
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    anim.tick();
    EXPECT_EQ(anim.currentIndex(), 1u);

    // Advance past last frame -> stops, returns false
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    bool changed = anim.tick();
    EXPECT_FALSE(changed);
    EXPECT_FALSE(anim.isPlaying());
}

TEST(SixelAnimationTest, SpeedMultiplier) {
    SixelAnimation anim;
    EXPECT_FLOAT_EQ(anim.speed(), 1.0f);

    anim.setSpeed(2.0f);
    EXPECT_FLOAT_EQ(anim.speed(), 2.0f);

    // Speed affects frame advancement: 2x speed halves effective delay
    anim.addFrame(makeFrame(2, 2, 20)); // 20ms / 2.0 = 10ms effective
    anim.addFrame(makeFrame(2, 2, 20));
    anim.play();

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    bool changed = anim.tick();
    EXPECT_TRUE(changed);
    EXPECT_EQ(anim.currentIndex(), 1u);
}

TEST(SixelAnimationTest, TotalDurationMs) {
    SixelAnimation anim;
    EXPECT_EQ(anim.totalDurationMs(), 0);

    anim.addFrame(makeFrame(2, 2, 100));
    anim.addFrame(makeFrame(2, 2, 200));
    anim.addFrame(makeFrame(2, 2, 50));
    EXPECT_EQ(anim.totalDurationMs(), 350);
}

TEST(SixelAnimationTest, Clear) {
    SixelAnimation anim;
    anim.addFrame(makeFrame(2, 2));
    anim.addFrame(makeFrame(2, 2));
    anim.play();

    anim.clear();
    EXPECT_EQ(anim.frameCount(), 0u);
    EXPECT_EQ(anim.currentIndex(), 0u);
    EXPECT_FALSE(anim.isPlaying());
    EXPECT_EQ(anim.currentFrame(), nullptr);
}

TEST(SixelAnimationTest, TickWhenNotPlaying) {
    SixelAnimation anim;
    anim.addFrame(makeFrame(2, 2, 1));
    // Not playing, tick should return false
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(anim.tick());
}

// --- SixelAnimationDetector tests ---

TEST(SixelAnimationDetectorTest, SingleImageNotAnimation) {
    SixelAnimationDetector det;
    det.feedImage(0, 0, 100, 50);
    EXPECT_FALSE(det.isAnimation());
    EXPECT_EQ(det.sequenceFrameCount(), 1);
}

TEST(SixelAnimationDetectorTest, SamePositionIsAnimation) {
    SixelAnimationDetector det;
    det.feedImage(0, 0, 100, 50);
    EXPECT_FALSE(det.isAnimation());

    // Second image at same position
    bool matched = det.feedImage(0, 0, 100, 50);
    EXPECT_TRUE(matched);
    EXPECT_TRUE(det.isAnimation());
    EXPECT_EQ(det.sequenceFrameCount(), 2);

    // Third
    matched = det.feedImage(0, 0, 100, 50);
    EXPECT_TRUE(matched);
    EXPECT_EQ(det.sequenceFrameCount(), 3);
}

TEST(SixelAnimationDetectorTest, DifferentPositionResetsSequence) {
    SixelAnimationDetector det;
    det.feedImage(0, 0, 100, 50);
    det.feedImage(0, 0, 100, 50);
    EXPECT_TRUE(det.isAnimation());

    // Different position resets
    bool matched = det.feedImage(5, 0, 100, 50);
    EXPECT_FALSE(matched);
    EXPECT_FALSE(det.isAnimation());
    EXPECT_EQ(det.sequenceFrameCount(), 1);
}

TEST(SixelAnimationDetectorTest, DifferentSizeResetsSequence) {
    SixelAnimationDetector det;
    det.feedImage(0, 0, 100, 50);
    det.feedImage(0, 0, 100, 50);
    EXPECT_TRUE(det.isAnimation());

    // Same position but different size resets
    bool matched = det.feedImage(0, 0, 200, 50);
    EXPECT_FALSE(matched);
    EXPECT_FALSE(det.isAnimation());
}

TEST(SixelAnimationDetectorTest, Reset) {
    SixelAnimationDetector det;
    det.feedImage(0, 0, 100, 50);
    det.feedImage(0, 0, 100, 50);
    EXPECT_TRUE(det.isAnimation());

    det.reset();
    EXPECT_FALSE(det.isAnimation());
    EXPECT_EQ(det.sequenceFrameCount(), 0);
}
