#include <gtest/gtest.h>
#include "termcore/profile.h"
#include "termcore/config.h"

using namespace termcore;

TEST(ProfileTest, DefaultConstruction) {
    Profile p;
    EXPECT_TRUE(p.id.empty());
    EXPECT_TRUE(p.name.empty());
    EXPECT_TRUE(p.command.empty());
    EXPECT_TRUE(p.args.empty());
    EXPECT_TRUE(p.working_dir.empty());
    EXPECT_TRUE(p.icon.empty());
    EXPECT_FALSE(p.is_default);
    EXPECT_FALSE(p.hidden);
    EXPECT_FALSE(p.auto_detected);
    EXPECT_FALSE(p.theme.has_value());
    EXPECT_FALSE(p.font_family.has_value());
    EXPECT_FALSE(p.font_size.has_value());
    EXPECT_FALSE(p.cursor_style.has_value());
}

TEST(ProfileTest, ResolveProfileConfig_NoOverrides) {
    Config global;
    global.font_family = "Consolas";
    global.font_size = 14.0f;
    global.theme = "Catppuccin Mocha";
    global.cursor_style = "block";
    global.scrollback_limit = 5000;

    Profile p;
    p.id = "cmd";

    Config resolved = resolveProfileConfig(global, p);
    EXPECT_EQ(resolved.font_family, "Consolas");
    EXPECT_EQ(resolved.font_size, 14.0f);
    EXPECT_EQ(resolved.theme, "Catppuccin Mocha");
    EXPECT_EQ(resolved.cursor_style, "block");
    EXPECT_EQ(resolved.scrollback_limit, 5000);
    EXPECT_EQ(resolved.background, global.background);
    EXPECT_EQ(resolved.foreground, global.foreground);
}

TEST(ProfileTest, ResolveProfileConfig_WithOverrides) {
    Config global;
    global.font_family = "Consolas";
    global.font_size = 14.0f;
    global.theme = "Catppuccin Mocha";
    global.cursor_style = "block";

    Profile p;
    p.id = "powershell";
    p.theme = "One Dark";
    p.font_family = "Cascadia Code";

    Config resolved = resolveProfileConfig(global, p);
    EXPECT_EQ(resolved.font_family, "Cascadia Code");
    EXPECT_EQ(resolved.font_size, 14.0f);
    EXPECT_EQ(resolved.theme, "One Dark");
    EXPECT_EQ(resolved.cursor_style, "block");
}

TEST(ProfileTest, ResolveProfileConfig_AllOverrides) {
    Config global;
    global.font_family = "Consolas";
    global.font_size = 14.0f;
    global.theme = "Catppuccin Mocha";
    global.cursor_style = "block";

    Profile p;
    p.theme = "Dracula";
    p.font_family = "JetBrains Mono";
    p.font_size = 16.0f;
    p.cursor_style = "bar";

    Config resolved = resolveProfileConfig(global, p);
    EXPECT_EQ(resolved.font_family, "JetBrains Mono");
    EXPECT_EQ(resolved.font_size, 16.0f);
    EXPECT_EQ(resolved.theme, "Dracula");
    EXPECT_EQ(resolved.cursor_style, "bar");
}
