#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

#include <commctrl.h>
#include <algorithm>
#include <set>

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

namespace termcore {

// Control ID for the search EDIT
static constexpr int kSearchEditId = 1001;

// ---------------------------------------------------------------------------
// createSearchEdit - creates the Win32 EDIT control in the top bar
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::createSearchEdit() {
    if (searchEdit_) return;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right;

    int editX = (w - kUsSearchW) / 2 + 6;
    int editY = (kUsTopBarH - kUsSearchH) / 2 + 2;
    int editW = kUsSearchW - 12;
    int editH = kUsSearchH - 4;

    searchEdit_ = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        editX, editY, editW, editH,
        hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEditId)),
        (HINSTANCE)GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE),
        nullptr);

    if (searchEdit_) {
        // Set font to match the UI
        HFONT hFont = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        SendMessageW(searchEdit_, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Set cue banner (placeholder text)
        SendMessageW(searchEdit_, EM_SETCUEBANNER, TRUE,
                     (LPARAM)L"Search settings...");

        // Set background and text colors via WM_CTLCOLOREDIT in handleMessage
    }
}

// ---------------------------------------------------------------------------
// repositionSearchEdit - moves the EDIT control when window resizes
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::repositionSearchEdit(int windowWidth) {
    if (!searchEdit_) return;

    int editX = (windowWidth - kUsSearchW) / 2 + 6;
    int editY = (kUsTopBarH - kUsSearchH) / 2 + 2;
    int editW = kUsSearchW - 12;
    int editH = kUsSearchH - 4;

    SetWindowPos(searchEdit_, nullptr, editX, editY, editW, editH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
// onSearchTextChanged - called when the EDIT text changes
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onSearchTextChanged() {
    // Read current text from the EDIT control
    int len = GetWindowTextLengthW(searchEdit_);
    if (len > 0) {
        searchText_.resize(len);
        GetWindowTextW(searchEdit_, searchText_.data(), len + 1);
    } else {
        searchText_.clear();
    }

    // Perform search and rebuild visible categories
    rebuildVisibleCategories();
    scrollY_ = 0.f;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// clearSearch - clears the search text and resets filtering
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::clearSearch() {
    if (searchEdit_) {
        SetWindowTextW(searchEdit_, L"");
    }
    searchText_.clear();
    searchMatches_.clear();
    visibleCategoryIds_ = allCategoryIds_;
    scrollY_ = 0.f;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// rebuildVisibleCategories - filters categories based on search results
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::rebuildVisibleCategories() {
    if (!model_) return;

    if (searchText_.empty()) {
        searchMatches_.clear();
        visibleCategoryIds_ = allCategoryIds_;
        return;
    }

    // Convert wstring query to UTF-8 string for model_->search()
    std::string query;
    if (!searchText_.empty()) {
        int sz = WideCharToMultiByte(CP_UTF8, 0, searchText_.c_str(), -1,
                                     nullptr, 0, nullptr, nullptr);
        query.resize(sz - 1);
        WideCharToMultiByte(CP_UTF8, 0, searchText_.c_str(), -1,
                            query.data(), sz, nullptr, nullptr);
    }

    searchMatches_ = model_->search(query);

    // Collect unique category IDs that have matches
    std::set<std::string> matchedIds;
    for (const auto& m : searchMatches_) {
        matchedIds.insert(m.categoryId);
    }

    // Filter visibleCategoryIds_ to only those with matches,
    // preserving original order
    visibleCategoryIds_.clear();
    for (const auto& id : allCategoryIds_) {
        if (matchedIds.count(id)) {
            visibleCategoryIds_.push_back(id);
        }
    }

    // If selection is no longer visible, select first visible
    if (!visibleCategoryIds_.empty()) {
        bool selectionVisible = false;
        for (const auto& id : visibleCategoryIds_) {
            if (id == selectedCategoryId_) {
                selectionVisible = true;
                break;
            }
        }
        if (!selectionVisible) {
            selectedCategoryId_ = visibleCategoryIds_.front();
        }
    }
}

} // namespace termcore

#endif
