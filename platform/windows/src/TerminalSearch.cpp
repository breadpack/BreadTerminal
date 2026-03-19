#if defined(_WIN32)

#include "TerminalWindowState.h"

#include <commctrl.h>
#include <string>

namespace {

// Subclass ID for the search edit control
constexpr UINT_PTR kSearchEditSubclassId = 1;

// Search bar dimensions
constexpr int kSearchBarWidth = 300;
constexpr int kSearchBarHeight = 24;
constexpr int kSearchBarMargin = 8;

// Dark theme colors for the search bar
constexpr COLORREF kSearchBgColor = RGB(45, 45, 45);
constexpr COLORREF kSearchFgColor = RGB(220, 220, 220);

HBRUSH g_searchBgBrush = nullptr;

// Subclass procedure for the search EDIT control to handle Enter/Escape
LRESULT CALLBACK SearchEditSubclassProc(
        HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    auto* state = reinterpret_cast<TerminalWindowState*>(dwRefData);

    switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                state->closeSearch();
                return 0;
            }
            if (wParam == VK_RETURN) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (shift) {
                    state->searchPrev();
                } else {
                    state->searchNext();
                }
                return 0;
            }
            if (wParam == VK_F3) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (shift) {
                    state->searchPrev();
                } else {
                    state->searchNext();
                }
                return 0;
            }
            break;

        case WM_CHAR:
            // Prevent beep on Enter/Escape
            if (wParam == '\r' || wParam == 27) {
                return 0;
            }
            break;

        case WM_NCDESTROY:
            RemoveWindowSubclass(hWnd, SearchEditSubclassProc,
                                 kSearchEditSubclassId);
            break;
    }

    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

} // namespace

void TerminalWindowState::openSearch() {
    if (searchActive && searchEditHwnd) {
        // Already open, just focus
        SetFocus(searchEditHwnd);
        SendMessageW(searchEditHwnd, EM_SETSEL, 0, -1);
        return;
    }

    searchActive = true;

    RECT rc;
    GetClientRect(hwnd, &rc);

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

    // Subclass for Enter/Escape handling
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

void TerminalWindowState::closeSearch() {
    if (searchEditHwnd) {
        // Get and delete the font we created
        HFONT hFont = reinterpret_cast<HFONT>(
            SendMessageW(searchEditHwnd, WM_GETFONT, 0, 0));
        DestroyWindow(searchEditHwnd);
        if (hFont) {
            DeleteObject(hFont);
        }
        searchEditHwnd = nullptr;
    }

    searchActive = false;
    searchQuery.clear();
    terminalSearch.clear();
    currentMatchIndex = -1;

    // Clear search highlights from renderer
    if (renderer) {
        renderer->setSearchHighlights({}, -1);
    }

    SetFocus(hwnd);
    needsRender = true;
}

void TerminalWindowState::performSearch() {
    if (!searchEditHwnd || !screen) return;

    // Get text from edit control
    int len = GetWindowTextLengthW(searchEditHwnd);
    if (len <= 0) {
        terminalSearch.clear();
        currentMatchIndex = -1;
        if (renderer) {
            renderer->setSearchHighlights({}, -1);
        }
        needsRender = true;
        return;
    }

    searchQuery.resize(len + 1);
    GetWindowTextW(searchEditHwnd, &searchQuery[0], len + 1);
    searchQuery.resize(len);

    // Convert wide string to UTF-8 for core search API
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
        searchQuery.c_str(), static_cast<int>(searchQuery.size()),
        nullptr, 0, nullptr, nullptr);
    std::string utf8Query(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0,
        searchQuery.c_str(), static_cast<int>(searchQuery.size()),
        &utf8Query[0], utf8Len, nullptr, nullptr);

    // Use core search engine
    termcore::SearchOptions opts;
    opts.case_sensitive = false;
    opts.wrap_around = true;
    opts.search_scrollback = true;

    terminalSearch.search(*screen, utf8Query, opts);

    // Convert matches to renderer highlights (only visible rows)
    std::vector<D3DTextRenderer::SearchHighlight> highlights;
    int visibleCurrentIndex = -1;
    int viewportOffset = screen->viewportOffset();

    const auto& matches = terminalSearch.matches();
    for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
        const auto& m = matches[i];
        // Convert match row to viewport-relative row
        // Visible screen rows are 0..rows-1
        // Match rows: 0..rows-1 for screen, negative for scrollback
        int visRow = m.row;
        if (viewportOffset > 0) {
            // When scrolled up, viewport shows scrollback lines
            // viewportOffset lines of scrollback are visible
            // scrollback row -1 = most recent scrollback line
            // When viewportOffset=N, the viewport shows:
            //   row 0 on screen = scrollback line (N-1) = search row -(N)
            //   ...
            //   row N-1 on screen = scrollback line 0 = search row -1
            //   row N on screen = screen row 0
            if (m.row < 0) {
                // Scrollback match: -row maps to scrollback index (-row - 1)
                // Visible if scrollback index < viewportOffset
                int sbIndex = -m.row - 1;  // 0 = most recent
                // In viewport: most recent scrollback at bottom of scrollback region
                // sbIndex 0 (most recent) shows at visRow = viewportOffset - 1
                // sbIndex (viewportOffset-1) shows at visRow = 0
                visRow = viewportOffset - 1 - sbIndex;
            } else {
                // Screen row with scrollback offset
                visRow = m.row + viewportOffset;
            }
        }

        if (visRow >= 0 && visRow < screen->rows()) {
            if (terminalSearch.currentIndex() == i) {
                visibleCurrentIndex = static_cast<int>(highlights.size());
            }
            D3DTextRenderer::SearchHighlight h;
            h.row = visRow;
            h.startCol = m.start_col;
            h.endCol = m.end_col;
            highlights.push_back(h);
        }
    }

    currentMatchIndex = terminalSearch.currentIndex();

    if (renderer) {
        renderer->setSearchHighlights(highlights, visibleCurrentIndex);
    }
    needsRender = true;
}

void TerminalWindowState::searchNext() {
    if (!terminalSearch.isActive()) return;

    terminalSearch.next();
    currentMatchIndex = terminalSearch.currentIndex();

    // Scroll to make current match visible if needed
    const auto* match = terminalSearch.currentMatch();
    if (match && screen) {
        if (match->row < 0) {
            // Scrollback match: scroll viewport to show it
            int sbIndex = -match->row - 1;
            int sbSize = static_cast<int>(screen->scrollbackSize());
            int targetOffset = sbIndex + 1;
            if (targetOffset > sbSize) targetOffset = sbSize;
            // Set viewport to show this scrollback line
            screen->scrollViewportToBottom();
            screen->scrollViewportUp(targetOffset);
        } else {
            // Screen row: make sure viewport is at bottom
            screen->scrollViewportToBottom();
        }
    }

    // Rebuild highlights with updated current index
    performSearch();
}

void TerminalWindowState::searchPrev() {
    if (!terminalSearch.isActive()) return;

    terminalSearch.prev();
    currentMatchIndex = terminalSearch.currentIndex();

    // Scroll to make current match visible if needed
    const auto* match = terminalSearch.currentMatch();
    if (match && screen) {
        if (match->row < 0) {
            int sbIndex = -match->row - 1;
            int sbSize = static_cast<int>(screen->scrollbackSize());
            int targetOffset = sbIndex + 1;
            if (targetOffset > sbSize) targetOffset = sbSize;
            screen->scrollViewportToBottom();
            screen->scrollViewportUp(targetOffset);
        } else {
            screen->scrollViewportToBottom();
        }
    }

    performSearch();
}

void TerminalWindowState::repositionSearchBar() {
    if (!searchEditHwnd) return;

    RECT rc;
    GetClientRect(hwnd, &rc);

    int x = rc.right - kSearchBarWidth - kSearchBarMargin;
    int y = kSearchBarMargin;

    SetWindowPos(searchEditHwnd, nullptr, x, y,
                 kSearchBarWidth, kSearchBarHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

#endif // _WIN32
