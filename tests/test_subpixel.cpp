#include <gtest/gtest.h>
#include "termcore/font/subpixel.h"

namespace termcore {
namespace {

// ---------------------------------------------------------------------------
// SubpixelMode string roundtrip
// ---------------------------------------------------------------------------

TEST(SubpixelTest, SubpixelModeToStringRoundtrip) {
    EXPECT_EQ(subpixelModeFromString(subpixelModeToString(SubpixelMode::None)), SubpixelMode::None);
    EXPECT_EQ(subpixelModeFromString(subpixelModeToString(SubpixelMode::RGB)),  SubpixelMode::RGB);
    EXPECT_EQ(subpixelModeFromString(subpixelModeToString(SubpixelMode::BGR)),  SubpixelMode::BGR);
    EXPECT_EQ(subpixelModeFromString(subpixelModeToString(SubpixelMode::VRGB)), SubpixelMode::VRGB);
    EXPECT_EQ(subpixelModeFromString(subpixelModeToString(SubpixelMode::VBGR)), SubpixelMode::VBGR);
    EXPECT_EQ(subpixelModeFromString(subpixelModeToString(SubpixelMode::Auto)), SubpixelMode::Auto);
}

TEST(SubpixelTest, SubpixelModeFromStringCaseInsensitive) {
    EXPECT_EQ(subpixelModeFromString("RGB"),  SubpixelMode::RGB);
    EXPECT_EQ(subpixelModeFromString("Bgr"),  SubpixelMode::BGR);
    EXPECT_EQ(subpixelModeFromString("NONE"), SubpixelMode::None);
    EXPECT_EQ(subpixelModeFromString("AUTO"), SubpixelMode::Auto);
}

TEST(SubpixelTest, SubpixelModeFromStringInvalidReturnsAuto) {
    EXPECT_EQ(subpixelModeFromString(""),        SubpixelMode::Auto);
    EXPECT_EQ(subpixelModeFromString("invalid"),  SubpixelMode::Auto);
    EXPECT_EQ(subpixelModeFromString("xyz"),      SubpixelMode::Auto);
}

// ---------------------------------------------------------------------------
// HintingMode string roundtrip
// ---------------------------------------------------------------------------

TEST(SubpixelTest, HintingModeToStringRoundtrip) {
    EXPECT_EQ(hintingModeFromString(hintingModeToString(HintingMode::None)),   HintingMode::None);
    EXPECT_EQ(hintingModeFromString(hintingModeToString(HintingMode::Slight)), HintingMode::Slight);
    EXPECT_EQ(hintingModeFromString(hintingModeToString(HintingMode::Medium)), HintingMode::Medium);
    EXPECT_EQ(hintingModeFromString(hintingModeToString(HintingMode::Full)),   HintingMode::Full);
    EXPECT_EQ(hintingModeFromString(hintingModeToString(HintingMode::Auto)),   HintingMode::Auto);
}

TEST(SubpixelTest, HintingModeFromStringCaseInsensitive) {
    EXPECT_EQ(hintingModeFromString("FULL"),    HintingMode::Full);
    EXPECT_EQ(hintingModeFromString("Slight"),  HintingMode::Slight);
    EXPECT_EQ(hintingModeFromString("MEDIUM"),  HintingMode::Medium);
}

TEST(SubpixelTest, HintingModeFromStringInvalidReturnsAuto) {
    EXPECT_EQ(hintingModeFromString(""),        HintingMode::Auto);
    EXPECT_EQ(hintingModeFromString("invalid"), HintingMode::Auto);
    EXPECT_EQ(hintingModeFromString("strong"),  HintingMode::Auto);
}

// ---------------------------------------------------------------------------
// System detection returns valid modes
// ---------------------------------------------------------------------------

TEST(SubpixelTest, DetectSystemSubpixelReturnsValidMode) {
    SubpixelMode mode = detectSystemSubpixel();
    // Just verify it's one of the valid enum values (not out of range)
    EXPECT_TRUE(mode == SubpixelMode::None ||
                mode == SubpixelMode::RGB ||
                mode == SubpixelMode::BGR ||
                mode == SubpixelMode::VRGB ||
                mode == SubpixelMode::VBGR ||
                mode == SubpixelMode::Auto);
}

TEST(SubpixelTest, DetectSystemHintingReturnsValidMode) {
    HintingMode mode = detectSystemHinting();
    EXPECT_TRUE(mode == HintingMode::None ||
                mode == HintingMode::Slight ||
                mode == HintingMode::Medium ||
                mode == HintingMode::Full ||
                mode == HintingMode::Auto);
}

// ---------------------------------------------------------------------------
// String output values are lowercase
// ---------------------------------------------------------------------------

TEST(SubpixelTest, ToStringOutputIsLowercase) {
    // Verify all toString outputs are lowercase (convention)
    std::string s = subpixelModeToString(SubpixelMode::RGB);
    EXPECT_EQ(s, "rgb");

    s = hintingModeToString(HintingMode::Full);
    EXPECT_EQ(s, "full");
}

} // namespace
} // namespace termcore
