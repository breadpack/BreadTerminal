#include "bench_lua.h"
#include "termcore/lua_engine.h"
#include "termcore/lua_config.h"
#include "termcore/lua_module.h"

// All module headers for registration benchmark
#include "lua_bindings/lua_tab_module.h"
#include "lua_bindings/lua_command_module.h"
#include "lua_bindings/lua_event_module.h"
#include "lua_bindings/lua_theme_module.h"
#include "lua_bindings/lua_url_module.h"
#include "lua_bindings/lua_mux_module.h"
#include "lua_bindings/lua_shader_module.h"
#include "lua_bindings/lua_search_module.h"
#include "lua_bindings/lua_clipboard_module.h"
#include "lua_bindings/lua_paste_module.h"
#include "lua_bindings/lua_notify_module.h"
#include "lua_bindings/lua_status_module.h"
#include "lua_bindings/lua_git_module.h"
#include "lua_bindings/lua_session_module.h"
#include "lua_bindings/lua_annotation_module.h"
#include "lua_bindings/lua_shell_module.h"
#include "lua_bindings/lua_workspace_module.h"
#include "lua_bindings/lua_settings_module.h"
#include "lua_bindings/lua_vi_module.h"
#include "lua_bindings/lua_quick_module.h"

#include <memory>
#include <string>

namespace bench {

static void registerAllModules(termcore::LuaEngine& engine) {
    engine.registerModule(std::make_shared<termcore::LuaTabModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaCommandModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaEventModule>());
    engine.registerModule(std::make_shared<termcore::LuaThemeModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaUrlModule>(nullptr, nullptr));
    engine.registerModule(std::make_shared<termcore::LuaMuxModule>(nullptr, nullptr));
    engine.registerModule(std::make_shared<termcore::LuaShaderModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaSearchModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaClipboardModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaPasteModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaNotifyModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaStatusModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaGitModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaSessionModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaAnnotationModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaShellModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaWorkspaceModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaSettingsModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaViModule>(nullptr));
    engine.registerModule(std::make_shared<termcore::LuaQuickModule>(nullptr));
}

void runLuaBenchmarks(BenchmarkRunner& runner) {
    // --- lua_engine_init ---
    // Measure LuaEngine construction time (ops/sec)
    {
        runner.run("lua_engine_init", "ops/sec", []() -> double {
            constexpr int ops = 100;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::LuaEngine engine;
                (void)engine.isValid();
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }

    // --- lua_module_registration ---
    // Register all 20 modules and initialize (ops/sec)
    {
        runner.run("lua_module_registration", "ops/sec", []() -> double {
            constexpr int ops = 50;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::LuaEngine engine;
                registerAllModules(engine);
                engine.initializeModules();
                engine.clearAllModules();
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }

    // --- lua_defaults_loading ---
    // loadDefaults() time (ops/sec)
    {
        runner.run("lua_defaults_loading", "ops/sec", []() -> double {
            constexpr int ops = 50;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::LuaEngine engine;
                registerAllModules(engine);
                engine.initializeModules();
                engine.loadDefaults();
                engine.clearAllModules();
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }

    // --- lua_event_dispatch_no_handlers ---
    // fireEvent with 0 registered handlers (ops/sec)
    {
        runner.run("lua_event_dispatch_no_handlers", "ops/sec", []() -> double {
            termcore::LuaEngine engine;
            registerAllModules(engine);
            engine.initializeModules();

            constexpr int ops = 100000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                engine.fireEvent(termcore::LuaEvent::OnBell);
            }
            double sec = t.elapsedSec();
            engine.clearAllModules();
            return ops / sec;
        });
    }

    // --- lua_event_dispatch_10_handlers ---
    // fireEvent with 10 registered handlers (ops/sec)
    {
        runner.run("lua_event_dispatch_10_handlers", "ops/sec", []() -> double {
            termcore::LuaEngine engine;
            registerAllModules(engine);
            engine.initializeModules();

            // Register 10 handlers for "bell" event
            engine.loadString(R"(
                for i = 1, 10 do
                    terminal.on("bell", function(data) end)
                end
            )");

            constexpr int ops = 50000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                engine.fireEvent(termcore::LuaEvent::OnBell);
            }
            double sec = t.elapsedSec();
            engine.clearAllModules();
            return ops / sec;
        });
    }

    // --- lua_simple_script ---
    // loadString("x = 1 + 2") execution (ops/sec)
    {
        runner.run("lua_simple_script", "ops/sec", []() -> double {
            termcore::LuaEngine engine;

            constexpr int ops = 10000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                engine.loadString("x = 1 + 2");
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }

    // --- lua_config_loading ---
    // loadConfigLuaString with typical config (ops/sec)
    {
        runner.run("lua_config_loading", "ops/sec", []() -> double {
            const std::string config_lua = R"(
                terminal.config({
                    font_family = "JetBrains Mono",
                    font_size = 14,
                    cursor_style = "block",
                    cursor_blink = true,
                    scrollback_lines = 10000,
                    copy_on_select = true,
                    window_opacity = 0.95,
                    padding = 8,
                })

                terminal.colorscheme("my_theme", {
                    background = 0x1e1e2e,
                    foreground = 0xcdd6f4,
                    cursor = 0xf5e0dc,
                })

                terminal.keymap("ctrl+t", "new_tab")
                terminal.keymap("ctrl+w", "close_tab")
                terminal.keymap("ctrl+shift+left", "prev_tab")
                terminal.keymap("ctrl+shift+right", "next_tab")
            )";

            constexpr int ops = 50;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::loadConfigLuaString(config_lua);
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }
}

} // namespace bench
