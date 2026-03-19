#if defined(_WIN32)

#include "TerminalWindowState.h"

#include <algorithm>

// --- Mouse protocol reporting ---

bool TerminalWindowState::sendMouseEvent(MouseEventType type,
                                         MouseButton button,
                                         int x, int y) {
    if (!screen || !pty) return false;
    if (screen->mouseMode() == MouseMode::None) return false;

    GridPos pos = pixelToGrid(x, y);
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    MouseEvent me;
    me.type = type;
    me.button = button;
    me.col = pos.col;
    me.row = pos.row;
    me.shift = shift;
    me.alt = alt;
    me.ctrl = ctrl;

    std::string seq = encodeMouseEvent(
        me, screen->mouseMode(), screen->mouseEncoding());
    if (seq.empty()) return false;

    sendPtyData(seq.data(), seq.size());
    return true;
}

// --- Keyboard input ---

void TerminalWindowState::handleKeyDown(WPARAM wParam, LPARAM /*lParam*/) {
    bool appCursor = screen && screen->appCursorKeys();
    const char* pfx = appCursor ? "\x1bO" : "\x1b[";
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Shift+key scrollback navigation
    if (shift && screen) {
        switch (wParam) {
            case VK_PRIOR:
                screen->scrollViewportUp(termRows);
                needsRender = true;
                return;
            case VK_NEXT:
                screen->scrollViewportDown(termRows);
                needsRender = true;
                return;
            case VK_HOME:
                screen->scrollViewportToTop();
                needsRender = true;
                return;
            case VK_END:
                screen->scrollViewportToBottom();
                needsRender = true;
                return;
        }
    }

    switch (wParam) {
        case VK_UP:
            { char s[3]={pfx[0],pfx[1],'A'}; sendPtyData(s,3); } return;
        case VK_DOWN:
            { char s[3]={pfx[0],pfx[1],'B'}; sendPtyData(s,3); } return;
        case VK_RIGHT:
            { char s[3]={pfx[0],pfx[1],'C'}; sendPtyData(s,3); } return;
        case VK_LEFT:
            { char s[3]={pfx[0],pfx[1],'D'}; sendPtyData(s,3); } return;
        case VK_RETURN:
            sendPtyData("\r", 1); return;
        case VK_BACK:
            sendPtyData("\x7f", 1); return;
        case VK_TAB:
            sendPtyData("\t", 1); return;
        case VK_ESCAPE:
            if (searchActive) { closeSearch(); return; }
            sendPtyData("\x1b", 1); return;
        case VK_HOME:
            sendPtyData("\x1b[H", 3); return;
        case VK_END:
            sendPtyData("\x1b[F", 3); return;
        case VK_PRIOR:
            sendPtyData("\x1b[5~", 4); return;
        case VK_NEXT:
            sendPtyData("\x1b[6~", 4); return;
        case VK_DELETE:
            sendPtyData("\x1b[3~", 4); return;
        case VK_F1:
            sendPtyData("\x1bOP", 3); return;
        case VK_F2:
            sendPtyData("\x1bOQ", 3); return;
        case VK_F3:
            if (searchActive) {
                if (shift) { searchPrev(); } else { searchNext(); }
                return;
            }
            sendPtyData("\x1bOR", 3); return;
        case VK_F4:
            sendPtyData("\x1bOS", 3); return;
        case VK_F5:
            sendPtyData("\x1b[15~", 5); return;
        case VK_F6:
            sendPtyData("\x1b[17~", 5); return;
        case VK_F7:
            sendPtyData("\x1b[18~", 5); return;
        case VK_F8:
            sendPtyData("\x1b[19~", 5); return;
        case VK_F9:
            sendPtyData("\x1b[20~", 5); return;
        case VK_F10:
            sendPtyData("\x1b[21~", 5); return;
        case VK_F11:
            sendPtyData("\x1b[23~", 5); return;
        case VK_F12:
            sendPtyData("\x1b[24~", 5); return;
        default:
            break;
    }

    // Ctrl+key
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl) {
        if (wParam == VK_OEM_PLUS || wParam == VK_ADD) {
            changeFontSize(1.0f);
            return;
        }
        if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) {
            changeFontSize(-1.0f);
            return;
        }
        if (wParam == '0' || wParam == VK_NUMPAD0) {
            resetFontSize();
            return;
        }
        if (wParam == 'F') {
            openSearch();
            return;
        }
        if (wParam == 'C' && hasSelection) {
            copySelectionToClipboard();
            clearSelection();
            return;
        }
        if (wParam == 'V') {
            pasteFromClipboard();
            return;
        }
        if (wParam >= 'A' && wParam <= 'Z') {
            char c = static_cast<char>(wParam - 'A' + 1);
            sendPtyData(&c, 1);
        }
    }
}

void TerminalWindowState::handleChar(WPARAM wParam) {
    if (searchActive) return;

    wchar_t wc = static_cast<wchar_t>(wParam);
    if (wc < 0x20 && wc != '\r' && wc != '\t') return;

    if (screen && !screen->isViewportAtBottom()) {
        screen->scrollViewportToBottom();
        needsRender = true;
    }

    char utf8[4];
    int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1,
                                  utf8, sizeof(utf8),
                                  nullptr, nullptr);
    if (len > 0) {
        sendPtyData(utf8, static_cast<size_t>(len));
    }
}

#endif // _WIN32
