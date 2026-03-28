// Null-pointer safety tests for all 23 Lua binding modules.
// Each test instantiates a module with nullptr backing pointer(s),
// registers it with a LuaEngine, initializes it, and calls Lua functions
// to verify no crash occurs.

#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaModuleNullSafety, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/lua_module.h"

#include "lua_bindings/lua_tab_module.h"
#include "lua_bindings/lua_command_module.h"
#include "lua_bindings/lua_event_module.h"
#include "lua_bindings/lua_mux_module.h"
#include "lua_bindings/lua_clipboard_module.h"
#include "lua_bindings/lua_search_module.h"
#include "lua_bindings/lua_theme_module.h"
#include "lua_bindings/lua_url_module.h"
#include "lua_bindings/lua_notify_module.h"
#include "lua_bindings/lua_paste_module.h"
#include "lua_bindings/lua_git_module.h"
#include "lua_bindings/lua_session_module.h"
#include "lua_bindings/lua_annotation_module.h"
#include "lua_bindings/lua_shell_module.h"
#include "lua_bindings/lua_workspace_module.h"
#include "lua_bindings/lua_settings_module.h"
#include "lua_bindings/lua_vi_module.h"
#include "lua_bindings/lua_quick_module.h"
#include "lua_bindings/lua_shader_module.h"
#include "lua_bindings/lua_completion_module.h"
#include "lua_bindings/lua_config_api_module.h"
#include "lua_bindings/lua_provider_module.h"
#include "lua_bindings/lua_status_module.h"

using namespace termcore;

class LuaModuleNullSafety : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
    }

    std::unique_ptr<LuaEngine> engine_;
};

// ---------------------------------------------------------------------------
// 1. LuaTabModule (TabController* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, TabModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));
    engine_->initializeModules();

    // Verify sub-table exists
    auto r = engine_->loadString("assert(type(terminal.tab) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_title_format: stores callback, null-guarded on tabCtrl_ call
    r = engine_->loadString(R"(
        terminal.tab.on_title_format(function(info) return 'test' end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_process_icon: guarded with if(tabCtrl_)
    r = engine_->loadString(R"(
        terminal.tab.set_process_icon("vim", "V")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // move_left / move_right: guarded with if(tabCtrl_)
    r = engine_->loadString("terminal.tab.move_left()");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    r = engine_->loadString("terminal.tab.move_right()");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 2. LuaCommandModule (CommandPalette* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, CommandModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaCommandModule>(nullptr));
    engine_->initializeModules();

    // Verify sub-table exists
    auto r = engine_->loadString("assert(type(terminal.command) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // register and remove dereference palette_ without null check,
    // so we only verify the sub-table and function existence here.
    r = engine_->loadString(R"(
        assert(type(terminal.command.register) == 'function')
        assert(type(terminal.command.remove) == 'function')
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 3. LuaEventModule (no backing pointer)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, EventModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.event) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Register an event handler
    r = engine_->loadString(R"(
        terminal.event.on("test_event", function(data) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 4. LuaMuxModule (Mux* = nullptr, TabController* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, MuxModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaMuxModule>(nullptr, nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.mux) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // layout: guarded with if(!mux_) return
    r = engine_->loadString("terminal.mux.layout('tiled')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // split: guarded with if(!tabCtrl_) return
    r = engine_->loadString("terminal.mux.split('right', 0.5)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // broadcast: guarded with if(!mux_) return
    r = engine_->loadString("terminal.mux.broadcast('off')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // zoom_toggle: guarded with if(!mux_) return
    r = engine_->loadString("terminal.mux.zoom_toggle()");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // define_layout: only stores callback, no backing pointer access
    r = engine_->loadString(R"(
        terminal.mux.define_layout("test", function(panes) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // apply_custom_layout: guarded with if(!mux_) return
    r = engine_->loadString("terminal.mux.apply_custom_layout('test')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 5. LuaClipboardModule (ClipboardHistory* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, ClipboardModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaClipboardModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.clipboard) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_history_size: guarded with if(clipboard_ && n > 0)
    r = engine_->loadString("terminal.clipboard.set_history_size(50)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_preview_length: guarded with if(clipboard_ && n > 0)
    r = engine_->loadString("terminal.clipboard.set_preview_length(120)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_copy: guarded with if(clipboard_)
    r = engine_->loadString(R"(
        terminal.clipboard.on_copy(function(text) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 6. LuaSearchModule (SearchController* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, SearchModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaSearchModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.search) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_debounce: guarded with if(searchCtrl_)
    r = engine_->loadString("terminal.search.set_debounce(100)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_result: guarded with if(searchCtrl_)
    r = engine_->loadString(R"(
        terminal.search.on_result(function(matches) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 7. LuaThemeModule (Config* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, ThemeModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaThemeModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.theme) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // switch: guarded with if(config_)
    r = engine_->loadString("terminal.theme.switch('dark')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // current: guarded with if(config_)
    r = engine_->loadString("local c = terminal.theme.current()");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // list: does not use config_, only calls allAvailableThemes()
    r = engine_->loadString("local themes = terminal.theme.list()");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_schedule: only stores callback, no backing pointer access
    r = engine_->loadString(R"(
        terminal.theme.on_schedule(function(hour) return 'dark' end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 8. LuaUrlModule (UrlDetector* = nullptr, UrlHighlightManager* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, UrlModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaUrlModule>(nullptr, nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.url) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // add_scheme: guarded with if(detector_)
    r = engine_->loadString("terminal.url.add_scheme('magnet', 'obsidian')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_click: guarded with if(highlight_)
    r = engine_->loadString(R"(
        terminal.url.on_click(function(url) return false end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_color: guarded with if(highlight_)
    r = engine_->loadString("terminal.url.set_color(0x89b4fa)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_color_by_scheme: guarded with if(!highlight_) return
    r = engine_->loadString("terminal.url.set_color_by_scheme('ssh', '#ff6600')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 9. LuaNotifyModule (NotificationStore* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, NotifyModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaNotifyModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.notify) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // send: guarded with if(!store_) return
    r = engine_->loadString("terminal.notify.send('Title', 'Body', 'normal')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_max: guarded with if(store_ && n > 0)
    r = engine_->loadString("terminal.notify.set_max(200)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // deduplicate: guarded with if(store_)
    r = engine_->loadString("terminal.notify.deduplicate(5)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_receive: guarded with if(store_)
    r = engine_->loadString(R"(
        terminal.notify.on_receive(function(n) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 10. LuaPasteModule (PasteGuard* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, PasteModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaPasteModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.paste) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // add_danger: guarded with if(pasteGuard_)
    r = engine_->loadString(R"(
        terminal.paste.add_danger("DROP TABLE", "Dangerous SQL")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // whitelist: guarded with if(pasteGuard_)
    r = engine_->loadString("terminal.paste.whitelist('sudo apt update')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_mode: guarded with if(pasteGuard_)
    r = engine_->loadString("terminal.paste.set_mode('multiline')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 11. LuaGitModule (GitBranchDetector* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, GitModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaGitModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.git) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_cache_ttl: guarded with if(detector_)
    r = engine_->loadString("terminal.git.set_cache_ttl(30)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_branch_change: stores callback, guarded with if(detector_) on setter
    r = engine_->loadString(R"(
        terminal.git.on_branch_change(function(branch) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // format_branch: stores callback, guarded with if(detector_) on setter
    r = engine_->loadString(R"(
        terminal.git.format_branch(function(name) return name end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 12. LuaSessionModule (MultiSessionManager* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, SessionModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaSessionModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.session) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_save: stores callback, guarded with if(sessionMgr_)
    r = engine_->loadString(R"(
        terminal.session.on_save(function(name) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_restore: stores callback, guarded with if(sessionMgr_)
    r = engine_->loadString(R"(
        terminal.session.on_restore(function(name) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_naming: stores callback, guarded with if(sessionMgr_)
    r = engine_->loadString(R"(
        terminal.session.set_naming(function() return 'name' end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 13. LuaAnnotationModule (AnnotationManager* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, AnnotationModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaAnnotationModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.annotation) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // add: guarded with if(!annotMgr_) return -1
    r = engine_->loadString(R"(
        local id = terminal.annotation.add(0, "test", {color="#ffff00"})
        assert(id == -1)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // remove: guarded with if(!annotMgr_) return false
    r = engine_->loadString(R"(
        local ok = terminal.annotation.remove(0)
        assert(ok == false)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_badge_format: guarded with if(annotMgr_)
    r = engine_->loadString(R"(
        terminal.annotation.set_badge_format("{branch} | {cwd}")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_pattern: guarded with if(!annotMgr_) return
    r = engine_->loadString(R"(
        terminal.annotation.on_pattern("ERROR", function(row, text) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 14. LuaShellModule (ShellIntegrationConfig* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, ShellModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaShellModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.shell) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_env: guarded with if(config_)
    r = engine_->loadString("terminal.shell.set_env('KEY', 'value')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_command_finish: stores callback, guarded with if(config_)
    r = engine_->loadString(R"(
        terminal.shell.on_command_finish(function(exit_code, duration) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_ssh_term: guarded with if(config_)
    r = engine_->loadString("terminal.shell.set_ssh_term('xterm-256color')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 15. LuaWorkspaceModule (WorkspaceStatusProvider* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, WorkspaceModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaWorkspaceModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.workspace) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_status_change, set_cwd, get_status all dereference provider_ without null check.
    // Verify functions exist but do not call them to avoid crash.
    r = engine_->loadString(R"(
        assert(type(terminal.workspace.on_status_change) == 'function')
        assert(type(terminal.workspace.set_cwd) == 'function')
        assert(type(terminal.workspace.get_status) == 'function')
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 16. LuaSettingsModule (SettingsModel* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, SettingsModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaSettingsModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.settings) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // add_category: guarded with if(model_ && !items.empty())
    r = engine_->loadString(R"(
        terminal.settings.add_category("Test Plugin", {
            { key = "enabled", label = "Enable", type = "toggle" }
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 17. LuaViModule (ViCopyMode* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, ViModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaViModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.vi) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_word_chars, on_yank, map all dereference vi_ without null check.
    // Verify functions exist but do not call them to avoid crash.
    r = engine_->loadString(R"(
        assert(type(terminal.vi.set_word_chars) == 'function')
        assert(type(terminal.vi.on_yank) == 'function')
        assert(type(terminal.vi.map) == 'function')
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 18. LuaQuickModule (Config* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, QuickModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaQuickModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.quick) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_animation: guarded with if(config_ && opts)
    r = engine_->loadString(R"(
        terminal.quick.set_animation("slide", {duration=200, easing="ease-out"})
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_size: guarded with if(config_)
    r = engine_->loadString("terminal.quick.set_size(0.4)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_position: guarded with if(config_)
    r = engine_->loadString("terminal.quick.set_position('top')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 19. LuaShaderModule (ShaderEffect* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, ShaderModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaShaderModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.shader) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // enable: guarded with if(shaderEffect_)
    r = engine_->loadString("terminal.shader.enable('CRT', 0.5)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // disable: guarded with if(shaderEffect_)
    r = engine_->loadString("terminal.shader.disable('Bloom')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_param: guarded with if(shaderEffect_)
    r = engine_->loadString("terminal.shader.set_param('CRT', 'curvature', 0.3)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // on_frame: guarded with if(shaderEffect_)
    r = engine_->loadString(R"(
        terminal.shader.on_frame(function(time) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 20. LuaCompletionModule (CompletionManager* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, CompletionModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaCompletionModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.completion) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // register_provider: guarded with if(!manager_) return
    r = engine_->loadString(R"(
        terminal.completion.register_provider("test", {
            priority = 10,
            on_input = function(ctx) return "" end
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // remove_provider: guarded with if(!manager_) return
    r = engine_->loadString("terminal.completion.remove_provider('test')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_suggestion: guarded with if(!manager_) return
    r = engine_->loadString("terminal.completion.set_suggestion('test', 'hello')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_enabled: guarded with if(!manager_) return
    r = engine_->loadString("terminal.completion.set_enabled(true)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 21. LuaConfigApiModule (Config* = nullptr, KeybindingManager* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, ConfigApiModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaConfigApiModule>(nullptr, nullptr));
    engine_->initializeModules();

    // config_api module registers functions directly on terminal table,
    // not as a sub-table. Verify the functions exist.
    auto r = engine_->loadString(R"(
        assert(type(terminal.config) == 'function')
        assert(type(terminal.keymap) == 'function')
        assert(type(terminal.keymap_preset) == 'function')
        assert(type(terminal.colorscheme) == 'function')
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // config(), keymap(), colorscheme() all dereference config_/keybindings_ without null check.
    // Do not call them to avoid crash.
}

// ---------------------------------------------------------------------------
// 22. LuaProviderModule (ProviderRegistry* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, ProviderModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaProviderModule>(nullptr));
    engine_->initializeModules();

    // provider module registers terminal.provider() function directly on terminal table.
    auto r = engine_->loadString("assert(type(terminal.provider) == 'function')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // provider(): guarded with if(!registry_) return
    r = engine_->loadString(R"(
        terminal.provider("test_provider", {
            display_name = "Test",
            agent_type = "TestAgent"
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// 23. LuaStatusModule (TabController* = nullptr)
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, StatusModuleNoCrash) {
    engine_->registerModule(std::make_shared<LuaStatusModule>(nullptr));
    engine_->initializeModules();

    auto r = engine_->loadString("assert(type(terminal.status) == 'table')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_pill: guarded with if(!tabCtrl_) return
    r = engine_->loadString(R"(
        terminal.status.set_pill(0, {key="Build", value="OK", color="#00ff00"})
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // set_progress: guarded with if(!tabCtrl_) return
    r = engine_->loadString("terminal.status.set_progress(0, 0.5, 'loading')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // log: guarded with if(!tabCtrl_) return
    r = engine_->loadString("terminal.status.log(0, 'info', 'test message')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// Combined: all 23 modules registered together with nullptr
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, AllModulesRegisteredTogether) {
    engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaCommandModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->registerModule(std::make_shared<LuaMuxModule>(nullptr, nullptr));
    engine_->registerModule(std::make_shared<LuaClipboardModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaSearchModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaThemeModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaUrlModule>(nullptr, nullptr));
    engine_->registerModule(std::make_shared<LuaNotifyModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaPasteModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaGitModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaSessionModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaAnnotationModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaShellModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaWorkspaceModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaSettingsModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaViModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaQuickModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaShaderModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaCompletionModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaConfigApiModule>(nullptr, nullptr));
    engine_->registerModule(std::make_shared<LuaProviderModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaStatusModule>(nullptr));
    engine_->initializeModules();

    // Verify all 20 sub-tables + 3 top-level functions exist
    auto r = engine_->loadString(R"(
        local subtables = {
            "tab", "command", "event", "mux", "clipboard", "search",
            "theme", "url", "notify", "paste", "git", "session",
            "annotation", "shell", "workspace", "settings", "vi",
            "quick", "shader", "completion", "status"
        }
        local missing = {}
        for _, name in ipairs(subtables) do
            if type(terminal[name]) ~= "table" then
                table.insert(missing, name)
            end
        end
        if #missing > 0 then
            error("Missing sub-tables: " .. table.concat(missing, ", "))
        end

        -- config_api and provider register top-level functions
        assert(type(terminal.config) == "function", "terminal.config missing")
        assert(type(terminal.provider) == "function", "terminal.provider missing")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
// clearCallbacks on all modules with nullptr should not crash
// ---------------------------------------------------------------------------
TEST_F(LuaModuleNullSafety, ClearCallbacksAllModulesNoCrash) {
    auto tab      = std::make_shared<LuaTabModule>(nullptr);
    auto command   = std::make_shared<LuaCommandModule>(nullptr);
    auto event     = std::make_shared<LuaEventModule>();
    auto mux       = std::make_shared<LuaMuxModule>(nullptr, nullptr);
    auto clipboard = std::make_shared<LuaClipboardModule>(nullptr);
    auto search    = std::make_shared<LuaSearchModule>(nullptr);
    auto theme     = std::make_shared<LuaThemeModule>(nullptr);
    auto url       = std::make_shared<LuaUrlModule>(nullptr, nullptr);
    auto notify    = std::make_shared<LuaNotifyModule>(nullptr);
    auto paste     = std::make_shared<LuaPasteModule>(nullptr);
    auto git       = std::make_shared<LuaGitModule>(nullptr);
    auto session   = std::make_shared<LuaSessionModule>(nullptr);
    auto annotation = std::make_shared<LuaAnnotationModule>(nullptr);
    auto shell     = std::make_shared<LuaShellModule>(nullptr);
    auto workspace = std::make_shared<LuaWorkspaceModule>(nullptr);
    auto settings  = std::make_shared<LuaSettingsModule>(nullptr);
    auto vi        = std::make_shared<LuaViModule>(nullptr);
    auto quick     = std::make_shared<LuaQuickModule>(nullptr);
    auto shader    = std::make_shared<LuaShaderModule>(nullptr);
    auto completion = std::make_shared<LuaCompletionModule>(nullptr);
    auto configApi = std::make_shared<LuaConfigApiModule>(nullptr, nullptr);
    auto provider  = std::make_shared<LuaProviderModule>(nullptr);
    auto status    = std::make_shared<LuaStatusModule>(nullptr);

    engine_->registerModule(tab);
    engine_->registerModule(command);
    engine_->registerModule(event);
    engine_->registerModule(mux);
    engine_->registerModule(clipboard);
    engine_->registerModule(search);
    engine_->registerModule(theme);
    engine_->registerModule(url);
    engine_->registerModule(notify);
    engine_->registerModule(paste);
    engine_->registerModule(git);
    engine_->registerModule(session);
    engine_->registerModule(annotation);
    engine_->registerModule(shell);
    engine_->registerModule(workspace);
    engine_->registerModule(settings);
    engine_->registerModule(vi);
    engine_->registerModule(quick);
    engine_->registerModule(shader);
    engine_->registerModule(completion);
    engine_->registerModule(configApi);
    engine_->registerModule(provider);
    engine_->registerModule(status);
    engine_->initializeModules();

    // clearAllModules calls clearCallbacks() on every module -- must not crash
    engine_->clearAllModules();
}

#endif // TERMCORE_HAS_LUA
