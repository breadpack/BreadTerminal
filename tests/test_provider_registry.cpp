#include <gtest/gtest.h>
#include "termcore/provider_registry.h"

using namespace termcore;

TEST(ProviderRegistryTest, StartsEmpty) {
    ProviderRegistry registry;
    EXPECT_TRUE(registry.all().empty());
}

TEST(ProviderRegistryTest, RegisterAndFindById) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "claude_code";
    info.display_name = "Claude Code";
    info.agent_type = "ClaudeCode";
    info.detect_process = {"claude"};
    registry.registerProvider(info);
    auto* found = registry.findById("claude_code");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->display_name, "Claude Code");
    EXPECT_EQ(registry.all().size(), 1u);
}

TEST(ProviderRegistryTest, FindByAgentType) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "codex";
    info.agent_type = "Codex";
    registry.registerProvider(info);
    EXPECT_NE(registry.findByAgentType("Codex"), nullptr);
    EXPECT_EQ(registry.findByAgentType("Unknown"), nullptr);
}

TEST(ProviderRegistryTest, DetectByProcessName) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "claude_code";
    info.detect_process = {"claude"};
    registry.registerProvider(info);
    EXPECT_NE(registry.detect("claude", {}), nullptr);
    EXPECT_NE(registry.detect("Claude-Code", {}), nullptr);
    EXPECT_EQ(registry.detect("python", {}), nullptr);
}

TEST(ProviderRegistryTest, DetectByEnvVar) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "codex";
    info.detect_env = {"CODEX_SESSION"};
    registry.registerProvider(info);
    EXPECT_NE(registry.detect("node", {"CODEX_SESSION=1"}), nullptr);
    EXPECT_EQ(registry.detect("node", {"OTHER=1"}), nullptr);
}

TEST(ProviderRegistryTest, InstallState) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "claude_code";
    registry.registerProvider(info);
    EXPECT_FALSE(registry.isInstalled("claude_code"));
    registry.markInstalled("claude_code");
    EXPECT_TRUE(registry.isInstalled("claude_code"));
}

TEST(ProviderRegistryTest, ReplaceExistingProvider) {
    ProviderRegistry registry;
    ProviderInfo info1;
    info1.id = "claude_code";
    info1.display_name = "Old Name";
    registry.registerProvider(info1);
    ProviderInfo info2;
    info2.id = "claude_code";
    info2.display_name = "New Name";
    registry.registerProvider(info2);
    EXPECT_EQ(registry.all().size(), 1u);
    EXPECT_EQ(registry.findById("claude_code")->display_name, "New Name");
}

TEST(ProviderRegistryTest, HooksConfigEmpty) {
    ProviderHooksConfig config;
    EXPECT_TRUE(config.empty());
    config.config_dir = "~/.claude";
    EXPECT_FALSE(config.empty());
}
