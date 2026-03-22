#include "termcore/theme_preview.h"

#include <gtest/gtest.h>

TEST(ThemePreview, PreviewColorsFromTheme) {
    termcore::Theme theme{};
    theme.name = "Test";
    theme.background = 0x112233;
    theme.foreground = 0xaabbcc;
    theme.cursor_color = 0xdddddd;
    theme.selection_background = 0x445566;
    for (int i = 0; i < 16; ++i) {
        theme.palette[i] = static_cast<uint32_t>(i * 0x111111);
    }

    auto colors = termcore::previewColorsFromTheme(theme);
    EXPECT_EQ(colors.bg, 0x112233u);
    EXPECT_EQ(colors.fg, 0xaabbccu);
    EXPECT_EQ(colors.cursor, 0xddddddu);
    EXPECT_EQ(colors.selection_bg, 0x445566u);
    EXPECT_EQ(colors.palette[0], 0x000000u);
    EXPECT_EQ(colors.palette[1], 0x111111u);
    EXPECT_EQ(colors.palette[15], static_cast<uint32_t>(15 * 0x111111));
}

TEST(ThemePreview, InitiallyNotPreviewing) {
    termcore::ThemePreview preview;
    EXPECT_FALSE(preview.isPreviewing());
    EXPECT_FALSE(preview.previewedThemeName().has_value());
}

TEST(ThemePreview, SetPreviewTheme) {
    termcore::ThemePreview preview;
    termcore::Config config{};
    config.background = 0x000000;
    config.foreground = 0xFFFFFF;

    termcore::Theme theme{};
    theme.name = "PreviewTheme";
    theme.background = 0x112233;
    theme.foreground = 0xaabbcc;
    theme.cursor_color = 0xdddddd;
    theme.selection_background = 0x445566;
    theme.selection_foreground = 0xeeeeff;

    preview.setPreviewTheme(config, theme);

    EXPECT_TRUE(preview.isPreviewing());
    EXPECT_EQ(preview.previewedThemeName(), "PreviewTheme");
    EXPECT_EQ(config.background, 0x112233u);
    EXPECT_EQ(config.foreground, 0xaabbccu);
}

TEST(ThemePreview, RevertPreview) {
    termcore::ThemePreview preview;
    termcore::Config config{};
    config.background = 0x000000;
    config.foreground = 0xFFFFFF;
    config.cursor_color = 0x888888;
    config.selection_background = 0x333333;
    config.selection_foreground = 0xCCCCCC;
    for (int i = 0; i < 16; ++i) config.palette[i] = i;

    // Apply preview
    termcore::Theme theme{};
    theme.name = "Temp";
    theme.background = 0x112233;
    theme.foreground = 0xaabbcc;
    theme.cursor_color = 0xdddddd;
    theme.selection_background = 0x445566;
    theme.selection_foreground = 0xeeeeff;
    for (int i = 0; i < 16; ++i) theme.palette[i] = 0xFF0000 + i;

    preview.setPreviewTheme(config, theme);
    EXPECT_EQ(config.background, 0x112233u);

    // Revert
    preview.revertPreview(config);
    EXPECT_FALSE(preview.isPreviewing());
    EXPECT_EQ(config.background, 0x000000u);
    EXPECT_EQ(config.foreground, 0xFFFFFFu);
    EXPECT_EQ(config.cursor_color, 0x888888u);
    EXPECT_EQ(config.selection_background, 0x333333u);
    EXPECT_EQ(config.selection_foreground, 0xCCCCCCu);
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(config.palette[i], static_cast<uint32_t>(i));
    }
}

TEST(ThemePreview, RevertWithoutPreviewDoesNothing) {
    termcore::ThemePreview preview;
    termcore::Config config{};
    config.background = 0xABCDEF;

    preview.revertPreview(config);
    EXPECT_EQ(config.background, 0xABCDEFu);
    EXPECT_FALSE(preview.isPreviewing());
}

TEST(ThemePreview, MultiplePreviewsSaveOriginalConfig) {
    termcore::ThemePreview preview;
    termcore::Config config{};
    config.background = 0x000000;
    config.foreground = 0xFFFFFF;

    termcore::Theme theme1{};
    theme1.name = "Theme1";
    theme1.background = 0x111111;
    theme1.foreground = 0xEEEEEE;

    termcore::Theme theme2{};
    theme2.name = "Theme2";
    theme2.background = 0x222222;
    theme2.foreground = 0xDDDDDD;

    // First preview
    preview.setPreviewTheme(config, theme1);
    EXPECT_EQ(config.background, 0x111111u);

    // Second preview (should still save original, not the first preview)
    preview.setPreviewTheme(config, theme2);
    EXPECT_EQ(config.background, 0x222222u);
    EXPECT_EQ(preview.previewedThemeName(), "Theme2");

    // Revert should go back to original
    preview.revertPreview(config);
    EXPECT_EQ(config.background, 0x000000u);
    EXPECT_EQ(config.foreground, 0xFFFFFFu);
}
