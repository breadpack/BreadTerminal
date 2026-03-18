#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Declared in TerminalWindow.cpp
int runTerminalWindow(HINSTANCE hInstance, int nCmdShow);

int WINAPI wWinMain(HINSTANCE hInstance,
                    HINSTANCE /*hPrevInstance*/,
                    LPWSTR /*lpCmdLine*/,
                    int nCmdShow) {
    // Enable DPI awareness for crisp text rendering
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    return runTerminalWindow(hInstance, nCmdShow);
}

#endif // _WIN32
