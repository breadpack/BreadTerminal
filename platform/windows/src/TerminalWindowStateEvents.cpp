#if defined(_WIN32)

#include "TerminalWindowState.h"
#include <commctrl.h>
#include <thread>

#pragma comment(lib, "comctl32.lib")

namespace termcore {
    void positionImeWindow(HWND hwnd, int x, int y, int height);
}

// --- IPlatformHost implementation ---

void TerminalWindowState::invalidate() {
    needsRender = true;
    if (renderer) renderer->markContentDirty();
}

void TerminalWindowState::getViewportSize(int& w, int& h) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
}

std::string TerminalWindowState::getClipboardText() {
    std::string utf8;
    if (!OpenClipboard(hwnd)) return utf8;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* pData = static_cast<const wchar_t*>(GlobalLock(hData));
        if (pData) {
            int wlen = static_cast<int>(wcslen(pData));
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, pData, wlen,
                                               nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                utf8.resize(utf8Len);
                WideCharToMultiByte(CP_UTF8, 0, pData, wlen,
                                    &utf8[0], utf8Len, nullptr, nullptr);
            }
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return utf8;
}

void TerminalWindowState::setClipboardText(const std::string& text) {
    if (text.empty()) return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                    text.c_str(), static_cast<int>(text.size()),
                                    nullptr, 0);
    if (wlen <= 0) return;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
    if (!hMem) return;

    wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
    if (pMem) {
        MultiByteToWideChar(CP_UTF8, 0,
                            text.c_str(), static_cast<int>(text.size()),
                            pMem, wlen);
        pMem[wlen] = L'\0';
        GlobalUnlock(hMem);

        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, hMem);
            CloseClipboard();
        } else {
            GlobalFree(hMem);
        }
    } else {
        GlobalFree(hMem);
    }
}

void TerminalWindowState::setWindowTitle(const std::string& title) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                    title.c_str(), static_cast<int>(title.size()),
                                    nullptr, 0);
    if (wlen > 0) {
        std::wstring wtitle(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0,
                            title.c_str(), static_cast<int>(title.size()),
                            &wtitle[0], wlen);
        SetWindowTextW(hwnd, wtitle.c_str());
    }
}

void TerminalWindowState::closeWindow() {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

void TerminalWindowState::showConfirmDialog(const std::string& msg,
                                             std::function<void(bool)> cb) {
    // Run dialog in a separate thread to avoid blocking the event loop
    std::string capturedMsg = msg;
    HWND capturedHwnd = hwnd;
    std::thread([capturedMsg, capturedHwnd, cb = std::move(cb)]() {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                        capturedMsg.c_str(),
                                        static_cast<int>(capturedMsg.size()),
                                        nullptr, 0);
        std::wstring wmsg(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0,
                            capturedMsg.c_str(),
                            static_cast<int>(capturedMsg.size()),
                            &wmsg[0], wlen);

        int result = MessageBoxW(capturedHwnd, wmsg.c_str(),
                                 L"BreadTerminal",
                                 MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (cb) cb(result == IDYES);
    }).detach();
}

void TerminalWindowState::showSearchBar() {
    if (searchActive && searchEditHwnd) {
        SetFocus(searchEditHwnd);
        SendMessageW(searchEditHwnd, EM_SETSEL, 0, -1);
        return;
    }

    searchActive = true;

    RECT rc;
    GetClientRect(hwnd, &rc);

    constexpr int kSearchBarWidth = 300;
    constexpr int kSearchBarHeight = 24;
    constexpr int kSearchBarMargin = 8;

    int x = rc.right - kSearchBarWidth - kSearchBarMargin;
    int y = kSearchBarMargin;

    searchEditHwnd = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        x, y, kSearchBarWidth, kSearchBarHeight,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEditId)),
        GetModuleHandleW(nullptr),
        nullptr);

    if (!searchEditHwnd) {
        searchActive = false;
        return;
    }

    // Subclass for Enter/Escape handling (defined in TerminalWindowState.cpp)
    extern LRESULT CALLBACK SearchEditSubclassProc(
        HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    constexpr UINT_PTR kSearchEditSubclassId = 1;
    SetWindowSubclass(searchEditHwnd, SearchEditSubclassProc,
                      kSearchEditSubclassId,
                      reinterpret_cast<DWORD_PTR>(this));

    // Set font to match terminal feel
    HFONT hFont = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Consolas");
    if (hFont) {
        SendMessageW(searchEditHwnd, WM_SETFONT,
                     reinterpret_cast<WPARAM>(hFont), TRUE);
    }

    SetFocus(searchEditHwnd);
}

void TerminalWindowState::hideSearchBar() {
    if (searchEditHwnd) {
        HFONT hFont = reinterpret_cast<HFONT>(
            SendMessageW(searchEditHwnd, WM_GETFONT, 0, 0));
        DestroyWindow(searchEditHwnd);
        if (hFont) {
            DeleteObject(hFont);
        }
        searchEditHwnd = nullptr;
    }

    searchActive = false;

    // Clear search highlights from renderer
    if (renderer) {
        renderer->setSearchHighlights({}, -1);
    }

    SetFocus(hwnd);
    needsRender = true;
}

void TerminalWindowState::updateSearchResults(int current, int total) {
    // For now, we don't display match count in the search bar.
    // Could add a label next to the edit control in future.
    (void)current;
    (void)total;
    needsRender = true;
    if (renderer) renderer->markContentDirty();
}

void TerminalWindowState::setSearchBarText(const std::string& text) {
    if (!searchEditHwnd) return;

    // Convert UTF-8 to wide string
    if (text.empty()) {
        SetWindowTextW(searchEditHwnd, L"");
    } else {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                        text.c_str(), static_cast<int>(text.size()),
                                        nullptr, 0);
        if (wlen > 0) {
            std::wstring wtext(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0,
                                text.c_str(), static_cast<int>(text.size()),
                                &wtext[0], wlen);
            SetWindowTextW(searchEditHwnd, wtext.c_str());
            // Move cursor to end
            SendMessageW(searchEditHwnd, EM_SETSEL, wlen, wlen);
        }
    }
}

void TerminalWindowState::positionIME(int x, int y, int height) {
    termcore::positionImeWindow(hwnd, x, y, height);
}

void TerminalWindowState::onFontChanged(float cellW, float cellH) {
    if (cache) cache->clear();
    // Recreate atlas to free old font glyphs
    if (atlas) {
        atlas = std::make_unique<termcore::GlyphAtlas>();
        if (renderer) {
            renderer->setFontStack(fontCollection.get(), cache.get(),
                                   atlas.get(), rasterizer.get());
        }
    }
    needsRender = true;
    if (renderer) renderer->markContentDirty();
}

void TerminalWindowState::onColorsChanged() {
    applyTitleBarTheme(hwnd);
    updateTabBar();
    needsRender = true;
    if (renderer) renderer->markContentDirty();
}

void TerminalWindowState::onGridSizeChanged(int rows, int cols) {
    // Resize overlay
    showResizeOverlay = true;
    resizeOverlayStart = std::chrono::steady_clock::now();
    resizeOverlayCols = cols;
    resizeOverlayRows = rows;
    updateTabBar();
    needsRender = true;
    if (renderer) renderer->markContentDirty();
}

void TerminalWindowState::showNotification(const std::string& title,
                                            const std::string& body) {
    // TODO: Win32 notification toast
    (void)title;
    (void)body;
}

void TerminalWindowState::openSettingsWindow(const termcore::Config& config) {
    if (!unifiedSettings) {
        unifiedSettings = std::make_unique<termcore::UnifiedSettingsWindow>();
    }
    unifiedSettings->setConfig(config);
    unifiedSettings->setSaveCallback([this](const termcore::Config& updated) {
        if (controller) {
            controller->onConfigChanged(updated);
            // Refresh the unified settings window's own chrome colors
            if (unifiedSettings) {
                unifiedSettings->setConfig(controller->config());
            }
        }
    });
    unifiedSettings->show(hwnd);
}

float TerminalWindowState::dpiScale() {
    return dpiScale_;
}

std::unique_ptr<termcore::Pty> TerminalWindowState::createPty(
        const termcore::Profile& profile, int rows, int cols) {
    auto pty = termcore::createPty();
    if (!pty->spawn(profile.command, profile.args, profile.working_dir, rows, cols)) {
        OutputDebugStringW(L"BreadTerminal: failed to spawn shell for pane\n");
    }
    return pty;
}

void TerminalWindowState::repositionSearchBar() {
    if (!searchEditHwnd) return;

    constexpr int kSearchBarWidth = 300;
    constexpr int kSearchBarHeight = 24;
    constexpr int kSearchBarMargin = 8;

    RECT rc;
    GetClientRect(hwnd, &rc);

    int x = rc.right - kSearchBarWidth - kSearchBarMargin;
    int y = kSearchBarMargin;

    SetWindowPos(searchEditHwnd, nullptr, x, y,
                 kSearchBarWidth, kSearchBarHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

#endif // _WIN32
