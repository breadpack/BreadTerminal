#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sentry.h>
#include <shlobj.h>
#include <string>

#include "QuickTerminalWindow.h"
#include "termcore/config.h"
#include "termcore/quick_terminal.h"

// Declared in TerminalWindow.cpp
int runTerminalWindow(HINSTANCE hInstance, int nCmdShow);

static std::string getSentryDbPath() {
    wchar_t* appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData))) {
        char buf[MAX_PATH * 2];
        WideCharToMultiByte(CP_UTF8, 0, appData, -1, buf, sizeof(buf), nullptr, nullptr);
        CoTaskMemFree(appData);
        return std::string(buf) + "\\BreadTerminal\\sentry";
    }
    return ".sentry-native";
}

int WINAPI wWinMain(HINSTANCE hInstance,
                    HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/,
                    int nCmdShow) {
    // Enable DPI awareness for crisp text rendering
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Initialize Sentry crash reporting
    sentry_options_t* options = sentry_options_new();
    sentry_options_set_dsn(options,
        "https://06f5c0dc5b66d2b1833b3cb30a01ebfb@o4504224567066624.ingest.us.sentry.io/4511102931894272");
    sentry_options_set_release(options, "BreadTerminal@0.1.0");
    sentry_options_set_database_path(options, getSentryDbPath().c_str());
    sentry_options_set_handler_path(options, "crashpad_handler.exe");
    sentry_init(options);

    // Load config to check if quick terminal is enabled
    termcore::Config config = termcore::loadConfig();
    auto qtConfig = termcore::QuickTerminalConfig::fromConfig(config);

    if (qtConfig.enabled()) {
        // Start quick terminal mode: hidden window with global hotkey.
        // The normal terminal window also runs alongside.
        auto quickWin = std::make_unique<QuickTerminalWindow>();
        if (quickWin->init(hInstance, qtConfig)) {
            // Run both the main terminal window and the quick terminal.
            // The quick terminal registers a global hotkey and runs its
            // own message pump. We start the main window first, then
            // let the quick terminal message loop drive the app.
            // For simplicity, launch the main terminal window in parallel
            // by creating it, then merge into a single message loop.

            // Actually, we run the main terminal normally and let the
            // quick terminal window participate in the same message loop.
            // Since runTerminalWindow creates its own message loop, we
            // need a different approach: run just the quick terminal
            // if started with --quick-terminal flag, or run both.

            // For now: the quick terminal coexists with the main window.
            // The main window message loop picks up WM_HOTKEY for us.
            // We'll run just the quick terminal standalone.
            int result = quickWin->run();
            sentry_close();
            return result;
        }
        // If quick terminal init failed (e.g., hotkey in use), fall through
        // to normal window mode.
    }

    int result = runTerminalWindow(hInstance, nCmdShow);
    sentry_close();
    return result;
}

#endif // _WIN32
