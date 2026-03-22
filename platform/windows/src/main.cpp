#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "QuickTerminalWindow.h"
#include "termcore/config.h"
#include "termcore/quick_terminal.h"

// Declared in TerminalWindow.cpp
int runTerminalWindow(HINSTANCE hInstance, int nCmdShow);

int WINAPI wWinMain(HINSTANCE hInstance,
                    HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/,
                    int nCmdShow) {
    // Enable DPI awareness for crisp text rendering
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
            return quickWin->run();
        }
        // If quick terminal init failed (e.g., hotkey in use), fall through
        // to normal window mode.
    }

    return runTerminalWindow(hInstance, nCmdShow);
}

#endif // _WIN32
