#if defined(_WIN32)

#include "ClipboardHistoryPopup.h"

#include <algorithm>

#pragma comment(lib, "gdiplus.lib")

bool ClipboardHistoryPopup::classRegistered_ = false;

// --- Colors (dark theme) ---
namespace {

constexpr DWORD kBgColor       = 0xFF1E1E2E;
constexpr DWORD kBorderColor   = 0xFF45475A;
constexpr DWORD kItemHoverBg   = 0xFF313244;
constexpr DWORD kItemSelectedBg= 0xFF45475A;
constexpr DWORD kRecentAccent  = 0xFF89B4FA;
constexpr DWORD kTextPrimary   = 0xFFCDD6F4;
constexpr DWORD kTextSecondary = 0xFF6C7086;
constexpr DWORD kFilterBg      = 0xFF313244;

// Convert ARGB to Gdiplus::Color
Gdiplus::Color toColor(DWORD argb) {
    return Gdiplus::Color(
        static_cast<BYTE>((argb >> 24) & 0xFF),
        static_cast<BYTE>((argb >> 16) & 0xFF),
        static_cast<BYTE>((argb >> 8) & 0xFF),
        static_cast<BYTE>(argb & 0xFF));
}

} // namespace

// --- Construction / Destruction ---

ClipboardHistoryPopup::ClipboardHistoryPopup() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr);
}

ClipboardHistoryPopup::~ClipboardHistoryPopup() {
    close();
    if (gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

// --- Window class registration ---

void ClipboardHistoryPopup::registerWindowClass(HINSTANCE hInstance) {
    if (classRegistered_) return;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    classRegistered_ = true;
}

// --- Show / Close ---

void ClipboardHistoryPopup::show(
        HWND parent,
        const std::vector<termcore::ClipboardEntry>& entries,
        PasteCallback onPaste) {
    if (hwnd_) {
        SetFocus(hwnd_);
        return;
    }

    allEntries_ = entries;
    pasteCallback_ = std::move(onPaste);
    selectedIndex_ = 0;
    scrollOffset_ = 0;
    hoverIndex_ = -1;
    filterText_.clear();

    // Build unfiltered index list
    filteredIndices_.clear();
    int count = static_cast<int>(
        (std::min)(allEntries_.size(), static_cast<size_t>(kMaxVisible)));
    for (int i = 0; i < count; ++i) {
        filteredIndices_.push_back(i);
    }

    // Calculate popup height
    int visibleItems = static_cast<int>(filteredIndices_.size());
    int popupHeight = kFilterBarHeight + visibleItems * kItemHeight + kPadding * 2;
    popupHeight = (std::min)(popupHeight, kFilterBarHeight + kMaxVisible * kItemHeight + kPadding * 2);

    // Center on parent
    RECT parentRect;
    GetWindowRect(parent, &parentRect);
    int parentCx = (parentRect.left + parentRect.right) / 2;
    int parentCy = (parentRect.top + parentRect.bottom) / 2;
    int x = parentCx - kPopupWidth / 2;
    int y = parentCy - popupHeight / 2;

    registerWindowClass(GetModuleHandleW(nullptr));

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kClassName, L"",
        WS_POPUP,
        x, y, kPopupWidth, popupHeight,
        parent, nullptr, GetModuleHandleW(nullptr), this);

    if (!hwnd_) return;

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

void ClipboardHistoryPopup::close() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

// --- Window procedure ---

LRESULT CALLBACK ClipboardHistoryPopup::wndProc(
        HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ClipboardHistoryPopup* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<ClipboardHistoryPopup*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ClipboardHistoryPopup*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_PAINT:
            self->onPaint(hwnd);
            return 0;

        case WM_KEYDOWN:
            self->onKeyDown(hwnd, wParam);
            return 0;

        case WM_CHAR:
            self->onChar(hwnd, wParam);
            return 0;

        case WM_MOUSEMOVE:
            self->onMouseMove(hwnd, LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_LBUTTONDOWN:
            self->onLButtonDown(hwnd, LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_MOUSEWHEEL:
            self->onMouseWheel(hwnd, GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;

        case WM_KILLFOCUS:
            self->close();
            return 0;

        case WM_ERASEBKGND:
            return 1;  // We handle all painting

        case WM_DESTROY:
            self->hwnd_ = nullptr;
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Painting ---

void ClipboardHistoryPopup::onPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    // Double-buffer with GDI+
    Gdiplus::Bitmap buffer(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics g(&buffer);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Rounded-rect background
    float r = static_cast<float>(kCornerRadius), fw = static_cast<float>(w), fh = static_cast<float>(h);
    Gdiplus::GraphicsPath path;
    path.AddArc(0, 0, r*2, r*2, 180, 90);
    path.AddArc(fw-r*2, 0, r*2, r*2, 270, 90);
    path.AddArc(fw-r*2, fh-r*2, r*2, r*2, 0, 90);
    path.AddArc(0, fh-r*2, r*2, r*2, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush bgBrush(toColor(kBgColor));
    g.FillPath(&bgBrush, &path);
    g.DrawPath(&Gdiplus::Pen(toColor(kBorderColor), 1.0f), &path);
    g.SetClip(&path);

    // Fonts
    Gdiplus::FontFamily ff(L"Consolas");
    Gdiplus::Font previewFont(&ff, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font timeFont(&ff, 9, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font filterFont(&ff, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
    Gdiplus::Font recentFont(&ff, 9, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);

    // Filter bar
    float pad = static_cast<float>(kPadding);
    Gdiplus::RectF filterRect(pad, pad, fw - pad*2, static_cast<float>(kFilterBarHeight - kPadding));
    g.FillRectangle(&Gdiplus::SolidBrush(toColor(kFilterBg)), filterRect);

    Gdiplus::RectF filterTextRect(filterRect.X+8, filterRect.Y+2, filterRect.Width-16, filterRect.Height-4);
    Gdiplus::StringFormat sfLeft;
    sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
    sfLeft.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sfLeft.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    sfLeft.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush filterTxtBrush(toColor(filterText_.empty() ? kTextSecondary : kTextPrimary));
    g.DrawString(filterText_.empty() ? L"Type to filter..." : filterText_.c_str(),
                 -1, &filterFont, filterTextRect, &sfLeft, &filterTxtBrush);

    // Items
    int visibleCount = static_cast<int>(filteredIndices_.size());
    int maxItemsInView = (h - kFilterBarHeight) / kItemHeight;
    Gdiplus::SolidBrush textBrush(toColor(kTextPrimary)), dimBrush(toColor(kTextSecondary));
    Gdiplus::SolidBrush accentBrush(toColor(kRecentAccent));
    Gdiplus::StringFormat sfRight;
    sfRight.SetAlignment(Gdiplus::StringAlignmentFar);
    sfRight.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sfRight.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    for (int vi = 0; vi < maxItemsInView && vi < visibleCount; ++vi) {
        int dataIndex = filteredIndices_[scrollOffset_ + vi];
        if (scrollOffset_ + vi >= visibleCount) break;

        float iy = static_cast<float>(kFilterBarHeight + vi * kItemHeight);
        Gdiplus::RectF itemRect(0, iy, fw, static_cast<float>(kItemHeight));

        int idx = scrollOffset_ + vi;
        bool isSel = (idx == selectedIndex_), isHov = (idx == hoverIndex_);
        if (isSel)       g.FillRectangle(&Gdiplus::SolidBrush(toColor(kItemSelectedBg)), itemRect);
        else if (isHov)  g.FillRectangle(&Gdiplus::SolidBrush(toColor(kItemHoverBg)), itemRect);

        const auto& entry = allEntries_[dataIndex];
        float halfH = static_cast<float>(kItemHeight / 2);

        // Accent bar for most recent entry
        if (dataIndex == 0)
            g.FillRectangle(&Gdiplus::SolidBrush(toColor(kRecentAccent)),
                            Gdiplus::RectF(0, iy, 3.0f, static_cast<float>(kItemHeight)));

        // Preview text
        std::wstring preview = toWide(entry.preview);
        Gdiplus::RectF previewRect(pad + 6, iy + 4, fw - pad*2 - 12, halfH);
        g.DrawString(preview.c_str(), -1, &previewFont, previewRect, &sfLeft, &textBrush);

        // Timestamp
        std::wstring timeStr = formatRelativeTime(entry.timestamp);
        Gdiplus::RectF timeRect(pad + 6, iy + halfH + 2, fw * 0.5f, halfH - 6);
        g.DrawString(timeStr.c_str(), -1, &timeFont, timeRect, &sfLeft, &dimBrush);

        // "Latest" badge
        if (dataIndex == 0) {
            Gdiplus::RectF badgeRect(fw - 70, iy + halfH + 2, 60, halfH - 6);
            g.DrawString(L"latest", -1, &recentFont, badgeRect, &sfRight, &accentBrush);
        }

        // Separator
        if (vi < maxItemsInView - 1 && idx < visibleCount - 1) {
            float sy = iy + static_cast<float>(kItemHeight) - 0.5f;
            g.DrawLine(&Gdiplus::Pen(toColor(kBorderColor), 0.5f), pad, sy, fw - pad, sy);
        }
    }

    // Empty state
    if (visibleCount == 0) {
        Gdiplus::RectF emptyRect(0, static_cast<float>(kFilterBarHeight), fw, fh - kFilterBarHeight);
        Gdiplus::StringFormat sfC;
        sfC.SetAlignment(Gdiplus::StringAlignmentCenter);
        sfC.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"No clipboard history", -1, &previewFont, emptyRect, &sfC, &dimBrush);
    }

    // Scrollbar thumb
    if (visibleCount > maxItemsInView && maxItemsInView > 0) {
        float listH = fh - kFilterBarHeight;
        float thumbH = (std::max)(listH * maxItemsInView / static_cast<float>(visibleCount), 20.0f);
        float thumbY = kFilterBarHeight + (listH - thumbH) * scrollOffset_ / (visibleCount - maxItemsInView);
        g.FillRectangle(&Gdiplus::SolidBrush(Gdiplus::Color(80,255,255,255)),
                        Gdiplus::RectF(fw-4, thumbY, 3, thumbH));
    }

    Gdiplus::Graphics screen(hdc);
    screen.DrawImage(&buffer, 0, 0);
    EndPaint(hwnd, &ps);
}

// --- Keyboard ---

void ClipboardHistoryPopup::onKeyDown(HWND hwnd, WPARAM wParam) {
    int n = static_cast<int>(filteredIndices_.size());
    if (n == 0 && wParam != VK_ESCAPE && wParam != VK_BACK) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int pageSize = (rc.bottom - rc.top - kFilterBarHeight) / kItemHeight;
    bool changed = true;

    switch (wParam) {
        case VK_UP:
            if (selectedIndex_ > 0) selectedIndex_--;
            break;
        case VK_DOWN:
            if (selectedIndex_ < n - 1) selectedIndex_++;
            break;
        case VK_HOME:   selectedIndex_ = 0; break;
        case VK_END:    selectedIndex_ = n - 1; break;
        case VK_PRIOR:  selectedIndex_ = (std::max)(0, selectedIndex_ - pageSize); break;
        case VK_NEXT:   selectedIndex_ = (std::min)(n - 1, selectedIndex_ + pageSize); break;
        case VK_RETURN: selectAndPaste(); return;
        case VK_ESCAPE: close(); return;
        case VK_BACK:
            if (!filterText_.empty()) { filterText_.pop_back(); updateFilter(); }
            break;
        default: changed = false; break;
    }

    // Keep selection visible
    if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
    if (selectedIndex_ >= scrollOffset_ + pageSize) scrollOffset_ = selectedIndex_ - pageSize + 1;
    if (changed) InvalidateRect(hwnd, nullptr, FALSE);
}

void ClipboardHistoryPopup::onChar(HWND hwnd, WPARAM wParam) {
    // Ignore control characters except backspace (handled in onKeyDown)
    if (wParam < 32 || wParam == 127) return;

    filterText_ += static_cast<wchar_t>(wParam);
    updateFilter();
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Mouse ---

void ClipboardHistoryPopup::onMouseMove(HWND hwnd, int x, int y) {
    int newHover = hitTestItem(y);
    if (newHover != hoverIndex_) {
        hoverIndex_ = newHover;
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    // Track mouse leave for hover clearing
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);
}

void ClipboardHistoryPopup::onLButtonDown(HWND hwnd, int x, int y) {
    int clicked = hitTestItem(y);
    if (clicked >= 0 &&
        clicked < static_cast<int>(filteredIndices_.size())) {
        selectedIndex_ = clicked;
        selectAndPaste();
    }
}

void ClipboardHistoryPopup::onMouseWheel(HWND hwnd, int delta) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int pageSize = (rc.bottom - rc.top - kFilterBarHeight) / kItemHeight;
    int n = static_cast<int>(filteredIndices_.size());
    scrollOffset_ += (delta > 0 ? -3 : 3);
    scrollOffset_ = (std::max)(0, (std::min)(scrollOffset_, n - pageSize));
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Helpers ---

void ClipboardHistoryPopup::selectAndPaste() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(filteredIndices_.size())) {
        close(); return;
    }
    std::string text = allEntries_[filteredIndices_[selectedIndex_]].text;
    close();
    if (pasteCallback_ && !text.empty()) pasteCallback_(text);
}

void ClipboardHistoryPopup::updateFilter() {
    filteredIndices_.clear();
    selectedIndex_ = 0;
    scrollOffset_ = 0;

    if (filterText_.empty()) {
        int count = static_cast<int>(
            (std::min)(allEntries_.size(), static_cast<size_t>(kMaxVisible)));
        for (int i = 0; i < count; ++i) {
            filteredIndices_.push_back(i);
        }
        return;
    }

    // Case-insensitive substring match
    std::wstring lowerFilter = filterText_;
    for (auto& c : lowerFilter) c = towlower(c);

    for (int i = 0; i < static_cast<int>(allEntries_.size()); ++i) {
        std::wstring preview = toWide(allEntries_[i].preview);
        std::wstring lowerPreview;
        lowerPreview.resize(preview.size());
        for (size_t j = 0; j < preview.size(); ++j) {
            lowerPreview[j] = towlower(preview[j]);
        }

        if (lowerPreview.find(lowerFilter) != std::wstring::npos) {
            filteredIndices_.push_back(i);
        }
    }

    // Resize popup to fit filtered results
    if (hwnd_) {
        RECT rc;
        GetWindowRect(hwnd_, &rc);
        int visibleItems = static_cast<int>(filteredIndices_.size());
        if (visibleItems == 0) visibleItems = 1;  // room for "no results"
        int maxItems = (std::min)(visibleItems,
                                  static_cast<int>(kMaxVisible));
        int newHeight = kFilterBarHeight + maxItems * kItemHeight + kPadding * 2;

        // Keep centered horizontally, adjust height from top
        SetWindowPos(hwnd_, nullptr, 0, 0,
                     rc.right - rc.left, newHeight,
                     SWP_NOMOVE | SWP_NOZORDER);
    }
}

int ClipboardHistoryPopup::hitTestItem(int y) const {
    if (y < kFilterBarHeight) return -1;

    int itemIndex = (y - kFilterBarHeight) / kItemHeight + scrollOffset_;
    if (itemIndex >= 0 &&
        itemIndex < static_cast<int>(filteredIndices_.size())) {
        return itemIndex;
    }
    return -1;
}

std::wstring ClipboardHistoryPopup::toWide(const std::string& utf8) const {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
        nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
        &result[0], wlen);
    return result;
}

std::wstring ClipboardHistoryPopup::formatRelativeTime(
        std::chrono::system_clock::time_point tp) const {
    auto now = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - tp);
    int64_t secs = diff.count();

    if (secs < 0) return L"just now";
    if (secs < 60) return L"just now";
    if (secs < 3600) {
        int mins = static_cast<int>(secs / 60);
        return std::to_wstring(mins) + L"m ago";
    }
    if (secs < 86400) {
        int hours = static_cast<int>(secs / 3600);
        return std::to_wstring(hours) + L"h ago";
    }
    int days = static_cast<int>(secs / 86400);
    return std::to_wstring(days) + L"d ago";
}

#endif // _WIN32
