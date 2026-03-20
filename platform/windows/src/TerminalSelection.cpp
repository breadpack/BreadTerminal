#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "termcore/paste_guard.h"

#include <algorithm>
#include <shellapi.h>

// --- Selection / mouse helpers ---

TerminalWindowState::GridPos TerminalWindowState::pixelToGrid(int x, int y) const {
    GridPos pos;
    // Offset for tab bar when visible
    int offsetY = 0;
    if (mux) {
        auto* ws = mux->getWorkspace(wsId);
        if (ws && ws->tabs.size() > 1) {
            offsetY = static_cast<int>(cellHeight);
        }
    }
    pos.col = (std::max)(0, (std::min)(termCols - 1, static_cast<int>(x / cellWidth)));
    pos.row = (std::max)(0, (std::min)(termRows - 1, static_cast<int>((y - offsetY) / cellHeight)));
    return pos;
}

void TerminalWindowState::clearSelection() {
    hasSelection = false;
    isDragging = false;
    updateRendererSelection();
    needsRender = true;
}

void TerminalWindowState::updateRendererSelection() {
    if (!renderer) return;
    D3DTextRenderer::Selection sel;
    sel.active = hasSelection;
    sel.startRow = selectionStart.row;
    sel.startCol = selectionStart.col;
    sel.endRow = selectionEnd.row;
    sel.endCol = selectionEnd.col;
    renderer->setSelection(sel);
}

void TerminalWindowState::handleMouseDown(int x, int y) {
    // If mouse protocol is active, report to PTY instead of selecting
    if (sendMouseEvent(MouseEventType::Press, MouseButton::Left, x, y))
        return;

    // Clear any existing selection and start a new one
    GridPos pos = pixelToGrid(x, y);
    selectionStart = pos;
    selectionEnd = pos;
    hasSelection = false;
    isDragging = true;
    SetCapture(hwnd);
    updateRendererSelection();
    needsRender = true;
}

void TerminalWindowState::handleMouseMove(int x, int y) {
    // If mouse protocol is active, report drag to PTY
    if (sendMouseEvent(MouseEventType::Move, MouseButton::Left, x, y))
        return;

    if (!isDragging) return;
    GridPos pos = pixelToGrid(x, y);
    selectionEnd = pos;
    hasSelection = (selectionStart.row != selectionEnd.row ||
                    selectionStart.col != selectionEnd.col);
    updateRendererSelection();
    needsRender = true;
}

void TerminalWindowState::handleMouseUp(int x, int y) {
    // If mouse protocol is active, report release to PTY
    if (sendMouseEvent(MouseEventType::Release, MouseButton::Release, x, y))
        return;

    if (!isDragging) return;
    ReleaseCapture();
    isDragging = false;
    GridPos pos = pixelToGrid(x, y);
    selectionEnd = pos;
    hasSelection = (selectionStart.row != selectionEnd.row ||
                    selectionStart.col != selectionEnd.col);
    updateRendererSelection();
    needsRender = true;

    // Ctrl+click to open URL
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        int clickRow = pos.row;
        int clickCol = pos.col;
        std::string url = urlDetector.urlAt(detectedUrls, clickRow, clickCol);
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
}

void TerminalWindowState::handleDoubleClick(int x, int y) {
    if (!screen) return;
    GridPos pos = pixelToGrid(x, y);

    int row = pos.row;
    int startCol = pos.col;
    int endCol = pos.col;
    int cols = screen->cols();

    // Expand left/right while we have word characters
    auto isWordChar = [](char32_t cp) {
        return (cp >= 'A' && cp <= 'Z') ||
               (cp >= 'a' && cp <= 'z') ||
               (cp >= '0' && cp <= '9') ||
               cp == '_' || cp == '-' || cp == '.' ||
               cp > 127; // non-ASCII treated as word chars
    };

    while (startCol > 0) {
        const TermCell& c = screen->cellAt(row, startCol - 1);
        if (!isWordChar(c.codepoint)) break;
        --startCol;
    }
    while (endCol < cols - 1) {
        const TermCell& c = screen->cellAt(row, endCol + 1);
        if (!isWordChar(c.codepoint)) break;
        ++endCol;
    }

    selectionStart = {row, startCol};
    selectionEnd = {row, endCol};
    hasSelection = true;
    isDragging = false;
    updateRendererSelection();
    needsRender = true;
}

// --- Click-to-move cursor (shell integration) ---

std::string TerminalWindowState::handleClickToMoveCursor(int row, int col,
                                                          const Screen& scr) {
    // Only move cursor when we're at a shell prompt (OSC 133 state)
    PromptState state = scr.promptState();
    if (state != PromptState::Prompt && state != PromptState::Input)
        return {};

    int curRow = scr.cursorRow();
    int curCol = scr.cursorCol();

    // Clamp target to valid grid
    int maxCol = scr.cols() - 1;
    int maxRow = scr.rows() - 1;
    if (col < 0) col = 0;
    if (col > maxCol) col = maxCol;
    if (row < 0) row = 0;
    if (row > maxRow) row = maxRow;

    // Only allow horizontal movement on the cursor's current row.
    // Moving vertically past prompt boundaries could navigate into output,
    // so we restrict to the same line.
    if (row != curRow)
        return {};

    int delta = col - curCol;
    if (delta == 0)
        return {};

    std::string seq;
    if (delta > 0) {
        // Move right
        for (int i = 0; i < delta; ++i)
            seq += "\x1b[C";
    } else {
        // Move left
        for (int i = 0; i < -delta; ++i)
            seq += "\x1b[D";
    }

    return seq;
}

// --- Clipboard ---

std::string TerminalWindowState::getSelectedText() const {
    if (!hasSelection || !screen) return {};

    int sr = selectionStart.row, sc = selectionStart.col;
    int er = selectionEnd.row, ec = selectionEnd.col;

    // Normalize so start <= end in reading order
    if (sr > er || (sr == er && sc > ec)) {
        std::swap(sr, er);
        std::swap(sc, ec);
    }

    std::string result;
    for (int row = sr; row <= er; ++row) {
        int colStart = (row == sr) ? sc : 0;
        int colEnd = (row == er) ? ec : screen->cols() - 1;

        // Trim trailing spaces on each row
        int lastNonSpace = colStart - 1;
        for (int col = colStart; col <= colEnd; ++col) {
            const TermCell& cell = screen->cellAt(row, col);
            if (cell.codepoint != ' ' && cell.codepoint != 0) {
                lastNonSpace = col;
            }
        }

        for (int col = colStart; col <= lastNonSpace; ++col) {
            const TermCell& cell = screen->cellAt(row, col);
            char32_t cp = cell.codepoint;
            if (cp == 0) cp = ' ';

            // Encode codepoint as UTF-8
            if (cp < 0x80) {
                result += static_cast<char>(cp);
            } else if (cp < 0x800) {
                result += static_cast<char>(0xC0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                result += static_cast<char>(0xE0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                result += static_cast<char>(0xF0 | (cp >> 18));
                result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

        if (row < er) {
            result += '\n';
        }
    }
    return result;
}

void TerminalWindowState::copySelectionToClipboard() {
    std::string text = getSelectedText();
    if (text.empty()) return;

    // Convert UTF-8 to UTF-16
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

void TerminalWindowState::pasteFromClipboard() {
    // Read clipboard text into a local string, then close clipboard
    // before any dialogs so we don't hold the lock.
    std::string utf8;
    {
        if (!OpenClipboard(hwnd)) return;
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
    }

    if (utf8.empty()) return;

    bool bracketed = screen && screen->bracketedPaste();

    // Paste guard: check for dangerous content
    PasteGuard guard;
    PasteAnalysis analysis = guard.analyze(utf8, bracketed);
    if (analysis.danger == PasteDanger::Warn) {
        std::wstring msg = L"The clipboard content may be dangerous:\n\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::MultiLine))
            msg += L"  - Contains multiple lines\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::TrailingNewline))
            msg += L"  - Ends with a newline (will execute immediately)\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::SudoCommand))
            msg += L"  - Contains sudo command\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::RmRf))
            msg += L"  - Contains rm -rf command\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::CurlPipe))
            msg += L"  - Contains curl piped to shell\n";
        if (analysis.signals & static_cast<uint32_t>(PasteSignal::Base64Decode))
            msg += L"  - Contains base64 decode\n";
        msg += L"\nDo you want to paste anyway?";

        int result = MessageBoxW(hwnd, msg.c_str(),
                                 L"Paste Warning",
                                 MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (result != IDYES) return;
    }

    // Send paste data to PTY
    if (bracketed) {
        std::string wrapped = "\x1b[200~" + utf8 + "\x1b[201~";
        sendPtyData(wrapped.c_str(), wrapped.size());
    } else {
        sendPtyData(utf8.c_str(), utf8.size());
    }
}

#endif // _WIN32
