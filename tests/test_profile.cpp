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

class ProfileManagerTest : public ::testing::Test {
protected:
    ProfileManager mgr;
};

TEST_F(ProfileManagerTest, StartsEmpty) {
    EXPECT_TRUE(mgr.allProfiles().empty());
}

TEST_F(ProfileManagerTest, DefaultProfileFallback) {
    const Profile& def = mgr.defaultProfile();
    EXPECT_FALSE(def.command.empty());
#if defined(_WIN32)
    EXPECT_NE(def.command.find("cmd"), std::string::npos);
#endif
}

TEST_F(ProfileManagerTest, SetProfileAdds) {
    Profile p;
    p.id = "test-shell";
    p.name = "Test Shell";
    p.command = "/usr/bin/test";
    mgr.setProfile(p);
    EXPECT_EQ(mgr.allProfiles().size(), 1u);
    auto* found = mgr.findProfile("test-shell");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "Test Shell");
}

TEST_F(ProfileManagerTest, SetProfileUpdatesExisting) {
    Profile p1; p1.id = "my-shell"; p1.name = "v1"; p1.command = "/bin/sh";
    mgr.setProfile(p1);
    Profile p2; p2.id = "my-shell"; p2.name = "v2"; p2.command = "/bin/bash";
    mgr.setProfile(p2);
    EXPECT_EQ(mgr.allProfiles().size(), 1u);
    EXPECT_EQ(mgr.findProfile("my-shell")->command, "/bin/bash");
}

TEST_F(ProfileManagerTest, SetDefaultProfile) {
    Profile p1; p1.id = "a"; p1.name = "A"; p1.command = "/a"; mgr.setProfile(p1);
    Profile p2; p2.id = "b"; p2.name = "B"; p2.command = "/b"; mgr.setProfile(p2);
    mgr.setDefaultProfile("b");
    EXPECT_EQ(mgr.defaultProfile().id, "b");
}

TEST_F(ProfileManagerTest, DefaultProfileFirstMerged) {
    Profile p1; p1.id = "first"; p1.name = "First"; p1.command = "/first"; mgr.setProfile(p1);
    Profile p2; p2.id = "second"; p2.name = "Second"; p2.command = "/second"; mgr.setProfile(p2);
    EXPECT_EQ(mgr.defaultProfile().id, "first");
}

TEST_F(ProfileManagerTest, SetDefaultProfileNonexistent) {
    mgr.setDefaultProfile("nonexistent");
    const Profile& def = mgr.defaultProfile();
    EXPECT_FALSE(def.command.empty());
}

TEST_F(ProfileManagerTest, FindProfileUnknown) {
    EXPECT_EQ(mgr.findProfile("nope"), nullptr);
}

TEST_F(ProfileManagerTest, HideProfile) {
    Profile p; p.id = "hide-me"; p.name = "Hidden"; p.command = "/hidden";
    mgr.setProfile(p);
    mgr.hideProfile("hide-me");
    for (auto* vp : mgr.visibleProfiles()) {
        EXPECT_NE(vp->id, "hide-me");
    }
}

TEST_F(ProfileManagerTest, VisibleProfilesExcludesHidden) {
    Profile p1; p1.id = "a"; p1.name = "A"; p1.command = "/a"; mgr.setProfile(p1);
    Profile p2; p2.id = "b"; p2.name = "B"; p2.command = "/b"; mgr.setProfile(p2);
    Profile p3; p3.id = "c"; p3.name = "C"; p3.command = "/c"; mgr.setProfile(p3);
    mgr.hideProfile("b");
    auto visible = mgr.visibleProfiles();
    EXPECT_EQ(visible.size(), 2u);
    EXPECT_EQ(visible[0]->id, "a");
    EXPECT_EQ(visible[1]->id, "c");
}

TEST_F(ProfileManagerTest, MergeDetectedAndUser) {
    std::vector<Profile> detected;
    Profile d1; d1.id = "cmd"; d1.name = "cmd.exe"; d1.command = "cmd.exe"; d1.auto_detected = true;
    Profile d2; d2.id = "ps"; d2.name = "PowerShell"; d2.command = "powershell.exe"; d2.auto_detected = true;
    detected.push_back(d1);
    detected.push_back(d2);
    mgr.setDetectedProfiles(std::move(detected));

    Profile user_override; user_override.id = "ps"; user_override.theme = "One Dark";
    mgr.setProfile(user_override);

    Profile custom; custom.id = "my-ssh"; custom.name = "SSH"; custom.command = "ssh";
    mgr.setProfile(custom);

    auto all = mgr.allProfiles();
    EXPECT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].id, "cmd");
    EXPECT_EQ(all[1].id, "ps");
    EXPECT_EQ(all[2].id, "my-ssh");

    auto* ps = mgr.findProfile("ps");
    ASSERT_NE(ps, nullptr);
    EXPECT_EQ(ps->name, "PowerShell");
    EXPECT_TRUE(ps->theme.has_value());
    EXPECT_EQ(*ps->theme, "One Dark");
    EXPECT_FALSE(ps->auto_detected);
}

TEST_F(ProfileManagerTest, FieldLevelMerge) {
    std::vector<Profile> detected;
    Profile d; d.id = "bash"; d.name = "Bash"; d.command = "/bin/bash"; d.icon = "bash"; d.auto_detected = true;
    detected.push_back(d);
    mgr.setDetectedProfiles(std::move(detected));

    Profile user; user.id = "bash"; user.font_family = "Fira Code";
    mgr.setProfile(user);

    auto* merged = mgr.findProfile("bash");
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->name, "Bash");
    EXPECT_EQ(merged->command, "/bin/bash");
    EXPECT_EQ(merged->icon, "bash");
    EXPECT_TRUE(merged->font_family.has_value());
    EXPECT_EQ(*merged->font_family, "Fira Code");
}

TEST_F(ProfileManagerTest, FieldLevelMerge_CommandOverride) {
    std::vector<Profile> detected;
    Profile d; d.id = "ps"; d.name = "PowerShell"; d.command = "powershell.exe"; d.auto_detected = true;
    detected.push_back(d);
    mgr.setDetectedProfiles(std::move(detected));

    Profile user; user.id = "ps"; user.command = "pwsh.exe";
    mgr.setProfile(user);

    auto* merged = mgr.findProfile("ps");
    EXPECT_EQ(merged->command, "pwsh.exe");
    EXPECT_EQ(merged->name, "PowerShell");
}

#if TERMCORE_HAS_LUA
#include "termcore/lua_config.h"

TEST(ProfileLuaTest, LoadProfileFromLua) {
    std::string code = R"(
        terminal.profile({
            id = "test-ssh",
            name = "Test SSH",
            command = "ssh",
            args = { "user@host" },
            icon = "ssh",
            theme = "Dracula",
            font_size = 16,
        })
        terminal.default_profile("test-ssh")
        terminal.hide_profile("cmd")
    )";
    auto result = loadConfigLuaString(code);
    ASSERT_TRUE(result.ok()) << result.errorMessage();

    const Config& cfg = luaConfig();
    ASSERT_EQ(cfg.profiles.size(), 1u);
    EXPECT_EQ(cfg.profiles[0].id, "test-ssh");
    EXPECT_EQ(cfg.profiles[0].name, "Test SSH");
    EXPECT_EQ(cfg.profiles[0].command, "ssh");
    ASSERT_EQ(cfg.profiles[0].args.size(), 1u);
    EXPECT_EQ(cfg.profiles[0].args[0], "user@host");
    EXPECT_TRUE(cfg.profiles[0].theme.has_value());
    EXPECT_EQ(*cfg.profiles[0].theme, "Dracula");
    EXPECT_TRUE(cfg.profiles[0].font_size.has_value());
    EXPECT_FLOAT_EQ(*cfg.profiles[0].font_size, 16.0f);

    EXPECT_EQ(cfg.default_profile_id, "test-ssh");
    ASSERT_EQ(cfg.hidden_profile_ids.size(), 1u);
    EXPECT_EQ(cfg.hidden_profile_ids[0], "cmd");
}

TEST(ProfileLuaTest, ConfigWriterRoundTrip) {
    Config cfg;
    Profile p;
    p.id = "my-shell";
    p.name = "My Shell";
    p.command = "C:\\Program Files\\Git\\bin\\bash.exe";  // backslash test
    p.args = {"--login"};
    p.theme = "Dracula";
    p.font_size = 16.0f;
    cfg.profiles.push_back(p);
    cfg.default_profile_id = "my-shell";
    cfg.hidden_profile_ids.push_back("cmd");

    // Serialize
    std::string lua = serializeConfigLua(cfg);

    // Re-load
    auto result = loadConfigLuaString(lua);
    ASSERT_TRUE(result.ok()) << result.errorMessage();

    const Config& loaded = luaConfig();
    ASSERT_EQ(loaded.profiles.size(), 1u);
    EXPECT_EQ(loaded.profiles[0].id, "my-shell");
    EXPECT_EQ(loaded.profiles[0].command, "C:\\Program Files\\Git\\bin\\bash.exe");
    EXPECT_EQ(loaded.default_profile_id, "my-shell");
    ASSERT_EQ(loaded.hidden_profile_ids.size(), 1u);
    EXPECT_EQ(loaded.hidden_profile_ids[0], "cmd");
}
#endif
