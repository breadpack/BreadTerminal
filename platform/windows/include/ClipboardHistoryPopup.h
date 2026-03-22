#pragma once
#if defined(_WIN32)

#include "termcore/clipboard_history.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

/// A modal-like popup that displays clipboard history entries.
/// Rendered with GDI+ for dark theme, rounded corners, and smooth text.
class ClipboardHistoryPopup {
public:
    using PasteCallback = std::function<void(const std::string& text)>;

    ClipboardHistoryPopup();
    ~ClipboardHistoryPopup();

    /// Show the popup centered on the parent window.
    void show(HWND parent,
              const std::vector<termcore::ClipboardEntry>& entries,
              PasteCallback onPaste);

    /// Close and destroy the popup.
    void close();

    /// Register the window class (call once at startup).
    static void registerWindowClass(HINSTANCE hInstance);

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);

    // Event handlers
    void onPaint(HWND hwnd);
    void onKeyDown(HWND hwnd, WPARAM wParam);
    void onChar(HWND hwnd, WPARAM wParam);
    void onMouseMove(HWND hwnd, int x, int y);
    void onLButtonDown(HWND hwnd, int x, int y);
    void onMouseWheel(HWND hwnd, int delta);

    // Helpers
    void selectAndPaste();
    void updateFilter();
    int hitTestItem(int y) const;
    std::wstring toWide(const std::string& utf8) const;
    std::wstring formatRelativeTime(
        std::chrono::system_clock::time_point tp) const;

    // Layout
    static constexpr int kPopupWidth = 420;
    static constexpr int kItemHeight = 48;
    static constexpr int kFilterBarHeight = 32;
    static constexpr int kPadding = 8;
    static constexpr int kCornerRadius = 8;
    static constexpr int kMaxVisible = 20;

    // State
    HWND hwnd_ = nullptr;
    std::vector<termcore::ClipboardEntry> allEntries_;
    std::vector<int> filteredIndices_;  // indices into allEntries_
    int selectedIndex_ = 0;
    int scrollOffset_ = 0;
    int hoverIndex_ = -1;
    std::wstring filterText_;
    PasteCallback pasteCallback_;

    // GDI+ token
    ULONG_PTR gdiplusToken_ = 0;

    static constexpr wchar_t kClassName[] = L"BreadClipboardHistoryPopup";
    static bool classRegistered_;
};

#endif // _WIN32
