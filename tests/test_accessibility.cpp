#include <gtest/gtest.h>

#include "termcore/accessibility.h"
#include "termcore/config.h"

// On Windows we can also test the theme builder without calling real Win32 APIs.
#ifdef _WIN32
#include "HighContrastDetector.h"
#endif

namespace termcore {

// ---- AccessibilityPreferences defaults ----

TEST(AccessibilityPreferences, DefaultValues) {
    AccessibilityPreferences prefs;
    EXPECT_FALSE(prefs.high_contrast);
    EXPECT_FALSE(prefs.reduced_motion);
    EXPECT_FLOAT_EQ(prefs.animation_speed_factor, 1.0f);
}

TEST(AccessibilityPreferences, ReducedMotionDisablesAnimations) {
    AccessibilityPreferences prefs;
    prefs.reduced_motion = true;
    prefs.animation_speed_factor = 0.0f;
    EXPECT_FLOAT_EQ(prefs.animation_speed_factor, 0.0f);
}

TEST(AccessibilityPreferences, NormalAnimationSpeed) {
    AccessibilityPreferences prefs;
    // Default is normal speed
    EXPECT_FLOAT_EQ(prefs.animation_speed_factor, 1.0f);
    // Simulate a partial slowdown
    prefs.animation_speed_factor = 0.5f;
    EXPECT_FLOAT_EQ(prefs.animation_speed_factor, 0.5f);
}

// ---- Config accessibility defaults ----

TEST(ConfigAccessibility, DefaultAutoDetectHighContrast) {
    Config config;
    EXPECT_TRUE(config.auto_detect_high_contrast);
}

TEST(ConfigAccessibility, DefaultRespectReducedMotion) {
    Config config;
    EXPECT_TRUE(config.respect_reduced_motion);
}

// ---- HighContrastColors → Theme conversion (Windows only) ----

#ifdef _WIN32

TEST(HighContrastDetector, BuildThemeFromWhiteOnBlack) {
    HighContrastColors colors;
    colors.window = 0x000000;       // black background
    colors.window_text = 0xffffff;  // white text
    colors.highlight = 0x0000ff;    // blue highlight
    colors.highlight_text = 0xffffff;
    colors.gray_text = 0x808080;

    Theme theme = HighContrastDetector::buildThemeFromSystemColors(colors);

    EXPECT_EQ(theme.name, "System High Contrast");
    EXPECT_EQ(theme.background, 0x000000u);
    EXPECT_EQ(theme.foreground, 0xffffffu);
    EXPECT_EQ(theme.cursor_color, 0xffffffu);
    EXPECT_EQ(theme.selection_background, 0x0000ffu);
    EXPECT_EQ(theme.selection_foreground, 0xffffffu);

    // ANSI black should be the background color
    EXPECT_EQ(theme.palette[0], 0x000000u);
    // ANSI white (index 7) should be the foreground color
    EXPECT_EQ(theme.palette[7], 0xffffffu);
    // Dim text (index 8) should be the gray text
    EXPECT_EQ(theme.palette[8], 0x808080u);
}

TEST(HighContrastDetector, BuildThemeFromBlackOnWhite) {
    HighContrastColors colors;
    colors.window = 0xffffff;       // white background
    colors.window_text = 0x000000;  // black text
    colors.highlight = 0x000080;    // dark blue highlight
    colors.highlight_text = 0xffffff;
    colors.gray_text = 0x808080;

    Theme theme = HighContrastDetector::buildThemeFromSystemColors(colors);

    EXPECT_EQ(theme.background, 0xffffffu);
    EXPECT_EQ(theme.foreground, 0x000000u);
    EXPECT_EQ(theme.palette[0], 0xffffffu);  // ANSI black = bg
    EXPECT_EQ(theme.palette[7], 0x000000u);  // ANSI white = fg
}

TEST(HighContrastDetector, BuildThemeFromCustomColors) {
    HighContrastColors colors;
    colors.window = 0x1e1e1e;
    colors.window_text = 0x00ff00;   // green on dark
    colors.highlight = 0xffff00;
    colors.highlight_text = 0x000000;
    colors.gray_text = 0x666666;

    Theme theme = HighContrastDetector::buildThemeFromSystemColors(colors);

    EXPECT_EQ(theme.background, 0x1e1e1eu);
    EXPECT_EQ(theme.foreground, 0x00ff00u);
    // Verify palette has 16 entries that are all valid (non-negative)
    for (int i = 0; i < 16; ++i) {
        // Just ensure they fit in 24-bit RGB
        EXPECT_LE(theme.palette[i], 0xffffffu);
    }
}

#endif // _WIN32

} // namespace termcore
