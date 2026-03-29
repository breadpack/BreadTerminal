#if defined(_WIN32)

#include "TerminalWindowState.h"

using termcore::D3DTextRenderer;

#include <algorithm>
#include <shellapi.h>

// --- Win32 keycode to core keycode mapping ---

static uint32_t mapWin32Keycode(WPARAM wParam) {
    uint32_t keycode = static_cast<uint32_t>(wParam);

    // Map virtual keycodes to ASCII for printable keys
    if (keycode >= 'A' && keycode <= 'Z') {
        return keycode - 'A' + 'a';  // lowercase — return early to avoid VK_F1-F12 collision
    }

    switch (keycode) {
        // OEM keys -> ASCII
        case VK_OEM_4:     return '[';
        case VK_OEM_6:     return ']';
        case VK_OEM_PLUS:  return '=';
        case VK_OEM_MINUS: return '-';
        case VK_OEM_COMMA: return ',';
        case VK_OEM_PERIOD:return '.';
        case VK_OEM_1:     return ';';
        case VK_OEM_2:     return '/';
        case VK_OEM_3:     return '`';
        case VK_OEM_5:     return '\\';
        case VK_OEM_7:     return '\'';
        // Navigation keys -> core special keycodes
        case VK_UP:        return 0xF700;
        case VK_DOWN:      return 0xF701;
        case VK_LEFT:      return 0xF702;
        case VK_RIGHT:     return 0xF703;
        case VK_HOME:      return 0xF704;
        case VK_END:       return 0xF705;
        case VK_PRIOR:     return 0xF706;  // PageUp
        case VK_NEXT:      return 0xF707;  // PageDown
        case VK_TAB:       return 0xF708;
        case VK_RETURN:    return 0xF709;
        case VK_ESCAPE:    return 0xF70A;
        case VK_BACK:      return 0xF70B;
        case VK_SPACE:     return 0xF70C;
        case VK_DELETE:    return 0xF70D;
        // Function keys
        case VK_F1:        return 0xF710;
        case VK_F2:        return 0xF711;
        case VK_F3:        return 0xF712;
        case VK_F4:        return 0xF713;
        case VK_F5:        return 0xF714;
        case VK_F6:        return 0xF715;
        case VK_F7:        return 0xF716;
        case VK_F8:        return 0xF717;
        case VK_F9:        return 0xF718;
        case VK_F10:       return 0xF719;
        case VK_F11:       return 0xF71A;
        case VK_F12:       return 0xF71B;
        default:           return keycode;
    }
}

static uint8_t getWin32Mods() {
    uint8_t mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= termcore::ModShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= termcore::ModCtrl;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= termcore::ModAlt;
    return mods;
}

// --- Lock-free input enqueue (called from WndProc without lock) ---

void TerminalWindowState::enqueueKeyDown(WPARAM wParam) {
    InputEvent ev;
    ev.type = InputEvent::KeyDown;
    ev.wParam = wParam;
    ev.mods = getWin32Mods();  // capture modifiers at event time
    inputQueue_.push(ev);
}

void TerminalWindowState::enqueueChar(WPARAM wParam) {
    // Filter control chars early (same as handleChar)
    wchar_t wc = static_cast<wchar_t>(wParam);
    if (wc < 0x20) return;
    if (wc == 0x7F) return;  // DEL from Ctrl+Backspace — already handled in KeyDown
    if (searchActive) return;
    if (!controller) return;

    // If command palette or copy mode is active, queue for locked processing
    if (controller->commandPalette().isOpen() || controller->inCopyMode()) {
        InputEvent ev;
        ev.type = InputEvent::Char;
        ev.wParam = wParam;
        ev.mods = 0;
        inputQueue_.push(ev);
        return;
    }

    // Fast path: direct PTY write without any lock.
    // sendPtyData only writes to the OS pipe — no Screen mutation.
    char utf8[4];
    int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1,
                                  utf8, sizeof(utf8), nullptr, nullptr);
    if (len > 0) {
        controller->sendPtyData(utf8, static_cast<size_t>(len));
    }
}

void TerminalWindowState::drainInputQueue() {
    // Called under exclusive write lock (inside pollPty cycle)
    if (!controller) return;
    InputEvent ev;
    while (inputQueue_.pop(ev)) {
        if (ev.type == InputEvent::KeyDown) {
            // Use modifier captured at enqueue time, not current keyboard state
            termcore::KeyEvent ke;
            ke.keycode = mapWin32Keycode(ev.wParam);
            ke.modifiers = ev.mods;
            controller->onKeyEvent(ke);

            if (controller->needsRender()) {
                needsRender = true;
                controller->clearNeedsRender();
                if (renderer) renderer->markContentDirty();
            }
        } else {
            // Char event — handleChar still reads current state only for searchActive
            // but we already filtered that in enqueueChar
            handleChar(ev.wParam);
        }
    }
}

// --- Keyboard input ---

void TerminalWindowState::handleKeyDown(WPARAM wParam, LPARAM /*lParam*/) {
    if (!controller) return;

    termcore::KeyEvent ke;
    ke.keycode = mapWin32Keycode(wParam);
    ke.modifiers = getWin32Mods();
    controller->onKeyEvent(ke);

    if (controller->needsRender()) {
        needsRender = true;
        controller->clearNeedsRender();
        if (renderer) renderer->markContentDirty();
    }
}

void TerminalWindowState::handleChar(WPARAM wParam) {
    if (!controller) return;
    if (searchActive) return;

    wchar_t wc = static_cast<wchar_t>(wParam);
    // Filter control chars; \r and \t are already handled in handleKeyDown
    if (wc < 0x20) return;
    if (wc == 0x7F) return;  // DEL from Ctrl+Backspace — already handled in handleKeyDown

    char utf8[4];
    int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1,
                                  utf8, sizeof(utf8),
                                  nullptr, nullptr);
    if (len > 0) {
        std::string utf8str(utf8, static_cast<size_t>(len));
        controller->onCharInput(utf8str);

        if (controller->needsRender()) {
            needsRender = true;
            controller->clearNeedsRender();
        }
    }
}

// --- Mouse input ---

void TerminalWindowState::handleMouseDown(int x, int y) {
    if (!controller) return;

    // Sidebar click interception
    if (handleSidebarClick(x, y)) return;

    termcore::InputMouseEvent me;
    me.type = termcore::InputMouseEvent::Press;
    me.x = x;
    me.y = y;
    me.modifiers = getWin32Mods();
    me.button = 0; // left
    controller->onMouseEvent(me);

    // Capture mouse for drag
    SetCapture(hwnd);

    if (controller->needsRender()) {
        needsRender = true;
        controller->clearNeedsRender();
    }
}

void TerminalWindowState::handleMouseMove(int x, int y) {
    if (!controller) return;

    termcore::InputMouseEvent me;
    me.type = termcore::InputMouseEvent::Move;
    me.x = x;
    me.y = y;
    me.modifiers = getWin32Mods();
    me.button = 0;
    controller->onMouseEvent(me);

    if (controller->needsRender()) {
        needsRender = true;
        controller->clearNeedsRender();
    }
}

void TerminalWindowState::handleMouseUp(int x, int y) {
    if (!controller) return;

    ReleaseCapture();

    termcore::InputMouseEvent me;
    me.type = termcore::InputMouseEvent::Release;
    me.x = x;
    me.y = y;
    me.modifiers = getWin32Mods();
    me.button = 0;
    controller->onMouseEvent(me);

    // Ctrl+click to open URL
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        float cw = controller->cellWidth();
        float ch = controller->cellHeight();
        int col = static_cast<int>(x / cw);
        int row = static_cast<int>(y / ch);
        const auto& urls = controller->detectedUrls();
        termcore::UrlDetector detector;
        std::string url = detector.urlAt(urls, row, col);
        if (!url.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0,
                url.c_str(), static_cast<int>(url.size()),
                nullptr, 0);
            if (wlen > 0) {
                std::wstring wurl(wlen, L'\0');
                MultiByteToWideChar(CP_UTF8, 0,
                    url.c_str(), static_cast<int>(url.size()),
                    &wurl[0], wlen);
                ShellExecuteW(nullptr, L"open", wurl.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    }

    if (controller->needsRender()) {
        needsRender = true;
        controller->clearNeedsRender();
    }
}

void TerminalWindowState::handleDoubleClick(int x, int y) {
    if (!controller) return;

    termcore::InputMouseEvent me;
    me.type = termcore::InputMouseEvent::DoubleClick;
    me.x = x;
    me.y = y;
    me.modifiers = getWin32Mods();
    me.button = 0;
    controller->onMouseEvent(me);

    if (controller->needsRender()) {
        needsRender = true;
        controller->clearNeedsRender();
    }
}

void TerminalWindowState::handleMouseWheel(int delta, int x, int y) {
    if (!controller) return;

    int lines = (std::max)(1, std::abs(delta / WHEEL_DELTA) * 3);

    termcore::InputMouseEvent me;
    me.type = delta > 0 ? termcore::InputMouseEvent::ScrollUp
                        : termcore::InputMouseEvent::ScrollDown;
    me.x = x;
    me.y = y;
    me.modifiers = getWin32Mods();
    me.scrollLines = lines;
    controller->onMouseEvent(me);

    if (controller->needsRender()) {
        needsRender = true;
        controller->clearNeedsRender();
    }
}

// --- Tab bar interaction ---

bool TerminalWindowState::handleTabBarClick(int x, int y) {
    if (!controller || !renderer) return false;

    float cellH = controller->cellHeight();
    float cellW = controller->cellWidth();
    float tabBarH = cellH * controller->config().tab_bar_height;

    if (!controller->config().tab_bar_always_visible && controller->tabCount() <= 1)
        return false;
    if (y >= static_cast<int>(tabBarH))
        return false;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int viewW = rc.right - rc.left;
    int tabCount = controller->tabCount();

    float tabGap = 4.0f;
    float tabMinW = cellW * 12.0f;
    float tabMaxW = cellW * 24.0f;
    float leftMargin = 8.0f;
    float closeW = cellW * 1.5f;
    float plusBtnW = cellW * 2.0f;

    float availW = viewW - leftMargin - plusBtnW - 8.0f;
    float tabW = (availW - tabGap * (tabCount - 1)) / tabCount;
    tabW = (std::max)(tabMinW, (std::min)(tabMaxW, tabW));

    float fmx = static_cast<float>(x);

    // Check "+" button
    float plusX = leftMargin + tabCount * (tabW + tabGap) + 4.0f;
    if (fmx >= plusX && fmx < plusX + plusBtnW) {
        // New tab via controller
        termcore::KeyEvent ke;
        ke.keycode = 0; // not used
        ke.modifiers = 0;
        // Directly use tabs
        controller->tabs()->createTab(controller->termRows(), controller->termCols());
        updateTabBar();
        needsRender = true;
        return true;
    }

    // Check which tab was clicked
    for (int t = 0; t < tabCount; ++t) {
        float tabX = leftMargin + t * (tabW + tabGap);
        if (fmx >= tabX && fmx < tabX + tabW) {
            float closeHitX = tabX + tabW - closeW - 4.0f;
            if (fmx >= closeHitX) {
                // Close tab
                if (tabCount <= 1) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                } else {
                    auto tabs = controller->tabBarInfo();
                    // Find tab ID from mux
                    auto* mux = controller->tabs()->mux();
                    auto wsId = controller->tabs()->workspaceId();
                    auto* ws = mux->getWorkspace(wsId);
                    if (ws && t < static_cast<int>(ws->tabs.size())) {
                        mux->destroyTab(wsId, ws->tabs[t]->id);
                        controller->tabs()->syncActivePointers();
                    }
                    updateTabBar();
                    needsRender = true;
                }
            } else {
                // Switch tab and start drag tracking
                controller->tabs()->switchToTab(t);
                tabDragging = true;
                tabDragSourceIndex = t;
                tabDragStartX = x;
                SetCapture(hwnd);
                updateTabBar();
                needsRender = true;
            }
            return true;
        }
    }
    return true; // consumed (in tab bar area)
}

void TerminalWindowState::handleTabBarHover(int x, int y) {
    if (!controller || !renderer) return;

    float cellH = controller->cellHeight();
    float cellW = controller->cellWidth();
    float tabBarH = cellH * controller->config().tab_bar_height;

    if (!controller->config().tab_bar_always_visible && controller->tabCount() <= 1) return;

    auto tabInfo = renderer->getTabBar();

    if (y >= static_cast<int>(tabBarH)) {
        if (tabInfo.hovered_tab != -1 || tabInfo.hover_plus) {
            tabInfo.hovered_tab = -1;
            tabInfo.hover_close = false;
            tabInfo.hover_plus = false;
            renderer->setTabBar(tabInfo);
            needsRender = true;
        }
        return;
    }

    RECT rc;
    GetClientRect(hwnd, &rc);
    int viewW = rc.right - rc.left;
    int tabCount = controller->tabCount();

    float tabGap = 4.0f;
    float tabMinW = cellW * 12.0f;
    float tabMaxW = cellW * 24.0f;
    float leftMargin = 8.0f;
    float closeW = cellW * 1.5f;
    float plusBtnW = cellW * 2.0f;

    float availW = viewW - leftMargin - plusBtnW - 8.0f;
    float tabW = (availW - tabGap * (tabCount - 1)) / tabCount;
    tabW = (std::max)(tabMinW, (std::min)(tabMaxW, tabW));

    float fmx = static_cast<float>(x);
    int newHover = -1;
    bool newCloseHover = false;
    bool newPlusHover = false;

    for (int t = 0; t < tabCount; ++t) {
        float tabX = leftMargin + t * (tabW + tabGap);
        if (fmx >= tabX && fmx < tabX + tabW) {
            newHover = t;
            newCloseHover = (fmx >= tabX + tabW - closeW - 4.0f);
            break;
        }
    }

    float plusX = leftMargin + tabCount * (tabW + tabGap) + 4.0f;
    if (fmx >= plusX && fmx < plusX + plusBtnW) {
        newPlusHover = true;
    }

    if (tabInfo.hovered_tab != newHover
        || tabInfo.hover_close != newCloseHover
        || tabInfo.hover_plus != newPlusHover) {
        tabInfo.hovered_tab = newHover;
        tabInfo.hover_close = newCloseHover;
        tabInfo.hover_plus = newPlusHover;
        renderer->setTabBar(tabInfo);
        needsRender = true;
    }
}

bool TerminalWindowState::handleTabBarDrag(int x, int y) {
    if (!tabDragging || !controller || !renderer) return false;

    float cellW = controller->cellWidth();
    int tabCount = controller->tabCount();
    if (tabCount <= 1) {
        tabDragging = false;
        return false;
    }

    float tabGap = 4.0f;
    float tabMinW = cellW * 12.0f;
    float tabMaxW = cellW * 24.0f;
    float leftMargin = 8.0f;
    float plusBtnW = cellW * 2.0f;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int viewW = rc.right - rc.left;
    float availW = viewW - leftMargin - plusBtnW - 8.0f;
    float tabW = (availW - tabGap * (tabCount - 1)) / tabCount;
    tabW = (std::max)(tabMinW, (std::min)(tabMaxW, tabW));

    float fmx = static_cast<float>(x);

    // Determine which tab position the mouse is over
    int targetIndex = -1;
    for (int t = 0; t < tabCount; ++t) {
        float tabX = leftMargin + t * (tabW + tabGap);
        if (fmx >= tabX && fmx < tabX + tabW) {
            targetIndex = t;
            break;
        }
    }

    if (targetIndex >= 0 && targetIndex != tabDragSourceIndex) {
        auto* mux = controller->tabs()->mux();
        auto wsId = controller->tabs()->workspaceId();
        auto* ws = mux->getWorkspace(wsId);
        if (ws && tabDragSourceIndex < static_cast<int>(ws->tabs.size())) {
            auto tabId = ws->tabs[tabDragSourceIndex]->id;
            mux->moveTab(wsId, tabId, targetIndex);
            controller->tabs()->syncActivePointers();
            tabDragSourceIndex = targetIndex;
            updateTabBar();
            needsRender = true;
        }
    }
    return true;
}

void TerminalWindowState::handleTabBarDragEnd(int x, int y) {
    if (tabDragging) {
        tabDragging = false;
        tabDragSourceIndex = -1;
        ReleaseCapture();
    }
}

#endif // _WIN32
