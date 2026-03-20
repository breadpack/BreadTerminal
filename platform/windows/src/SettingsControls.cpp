#if defined(_WIN32)

#include "SettingsWindow.h"

#include <algorithm>
#include <cmath>

namespace termcore {

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

int SettingsWindow::hitTestTab(int mx, int my) const {
    int y0 = kSetTitleH;
    if (my < y0 || my >= y0 + kSetTabBarH) return -1;
    float tabW = (float)kSetWinWidth / kSetTabCount;
    int idx = (int)(mx / tabW);
    if (idx < 0 || idx >= kSetTabCount) return -1;
    return idx;
}

bool SettingsWindow::hitTestCloseButton(int mx, int my) const {
    int bx = kSetWinWidth - 32;
    return mx >= bx && mx < kSetWinWidth && my >= 4 && my < 28;
}

// ---------------------------------------------------------------------------
// Mouse handling
// ---------------------------------------------------------------------------

void SettingsWindow::onLButtonDown(int mx, int my) {
    // Close button
    if (hitTestCloseButton(mx, my)) {
        close();
        return;
    }

    // Title bar drag
    if (my < kSetTitleH) {
        ReleaseCapture();
        SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return;
    }

    // Tab click
    int tab = hitTestTab(mx, my);
    if (tab >= 0) {
        destroyTextEdit();
        activeTab_ = static_cast<SettingsTab>(tab);
        keysScrollY_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Content area clicks
    if (my > contentTop()) {
        // Commit any existing text edit first
        if (editCtrl_) commitTextEdit();

        switch (activeTab_) {
        case SettingsTab::General:    handleGeneralClick(mx, my); break;
        case SettingsTab::Appearance: handleAppearanceClick(mx, my); break;
        case SettingsTab::Font:       handleFontClick(mx, my); break;
        case SettingsTab::Keys:       handleKeysClick(mx, my); break;
        case SettingsTab::Clipboard:  handleClipboardClick(mx, my); break;
        }
    }
}

void SettingsWindow::onLButtonUp(int mx, int my) {
    if (sliderDragging_) {
        sliderDragging_ = false;
        ReleaseCapture();
        notifySave();
    }
}

void SettingsWindow::onMouseMove(int mx, int my) {
    if (sliderDragging_) {
        // Update opacity slider
        float fieldX = (float)(kSetPadding + kSetLabelW);
        float sliderW = (float)kSetSliderW;
        float val = ((float)mx - fieldX) / sliderW;
        val = (std::max)(0.f, (std::min)(1.f, val));
        config_.background_opacity = val;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// General tab click handling
// ---------------------------------------------------------------------------

void SettingsWindow::handleGeneralClick(int mx, int my) {
    float fx = (float)kSetPadding;
    float fy = (float)(contentTop() + kSetPadding);
    float fieldX = fx + kSetLabelW;
    float fieldW = (float)kSetFieldW;
    float row = (float)kSetRowHeight;

    // Shell text field (row 0)
    float shellY = fy;
    if (mx >= fieldX && mx < fieldX + fieldW &&
        my >= shellY && my < shellY + kSetFieldH) {
        std::wstring val = config_.shell.empty()
            ? L"" : toWide(config_.shell);
        beginTextEdit(0, fieldX, shellY, fieldW, val);
        return;
    }

    // Scrollback text field (row 1)
    float scrollY = fy + row;
    if (mx >= fieldX && mx < fieldX + fieldW &&
        my >= scrollY && my < scrollY + kSetFieldH) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%d", config_.scrollback_limit);
        beginTextEdit(1, fieldX, scrollY, fieldW, buf);
        return;
    }

    // Cursor Style pills (row 2)
    float cursorY = fy + row * 2;
    if (my >= cursorY && my < cursorY + kSetPillBtnH) {
        float pw = (float)kSetPillBtnW;
        float gap = (float)kSetPillGap;
        for (int i = 0; i < 3; ++i) {
            float px = fieldX + i * (pw + gap);
            if (mx >= px && mx < px + pw) {
                const char* styles[] = { "block", "underline", "bar" };
                config_.cursor_style = styles[i];
                notifySave();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    }

    // Cursor Blink toggle (row 3)
    float blinkY = fy + row * 3 + 2.f;
    if (mx >= fieldX && mx < fieldX + kSetToggleW &&
        my >= blinkY && my < blinkY + kSetToggleH) {
        config_.cursor_blink = !config_.cursor_blink;
        notifySave();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

// ---------------------------------------------------------------------------
// Appearance tab click handling
// ---------------------------------------------------------------------------

void SettingsWindow::handleAppearanceClick(int mx, int my) {
    float fx = (float)kSetPadding;
    float fy = (float)(contentTop() + kSetPadding);
    float fieldX = fx + kSetLabelW;
    float row = (float)kSetRowHeight;

    // Opacity slider (row 0)
    float sliderY = fy + 2.f;
    if (mx >= fieldX && mx < fieldX + kSetSliderW + 8 &&
        my >= sliderY && my < sliderY + kSetSliderH) {
        sliderDragging_ = true;
        SetCapture(hwnd_);
        float val = ((float)mx - fieldX) / (float)kSetSliderW;
        val = (std::max)(0.f, (std::min)(1.f, val));
        config_.background_opacity = val;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Blur pills (row 1)
    float blurY = fy + row;
    if (my >= blurY && my < blurY + kSetPillBtnH) {
        float pw = (float)kSetPillBtnW;
        float gap = (float)kSetPillGap;
        for (int i = 0; i < 4; ++i) {
            float px = fieldX + i * (pw + gap);
            if (mx >= px && mx < px + pw) {
                config_.background_blur = i;
                notifySave();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    }

    // Background color swatch (row 2)
    float bgColorY = fy + row * 2;
    if (mx >= fieldX && mx < fieldX + kSetSwatchSize &&
        my >= bgColorY && my < bgColorY + kSetSwatchSize) {
        openColorPicker(config_.background);
        return;
    }

    // Foreground color swatch (row 3)
    float fgColorY = fy + row * 3;
    if (mx >= fieldX && mx < fieldX + kSetSwatchSize &&
        my >= fgColorY && my < fgColorY + kSetSwatchSize) {
        openColorPicker(config_.foreground);
        return;
    }

    // Cursor color swatch (row 4)
    float cursorColorY = fy + row * 4;
    if (mx >= fieldX && mx < fieldX + kSetSwatchSize &&
        my >= cursorColorY && my < cursorColorY + kSetSwatchSize) {
        openColorPicker(config_.cursor_color);
        return;
    }
}

// ---------------------------------------------------------------------------
// Font tab click handling
// ---------------------------------------------------------------------------

void SettingsWindow::handleFontClick(int mx, int my) {
    float fx = (float)kSetPadding;
    float fy = (float)(contentTop() + kSetPadding);
    float fieldX = fx + kSetLabelW;
    float fieldW = (float)kSetFieldW;
    float row = (float)kSetRowHeight;

    // Font Family text field (row 0)
    float fontY = fy;
    if (mx >= fieldX && mx < fieldX + fieldW &&
        my >= fontY && my < fontY + kSetFieldH) {
        beginTextEdit(10, fieldX, fontY, fieldW, toWide(config_.font_family));
        return;
    }

    // Font Size text field (row 1)
    float sizeY = fy + row;
    if (mx >= fieldX && mx < fieldX + 80 &&
        my >= sizeY && my < sizeY + kSetFieldH) {
        wchar_t buf[16];
        swprintf(buf, 16, L"%.1f", config_.font_size);
        beginTextEdit(11, fieldX, sizeY, 80.f, buf);
        return;
    }

    // Minus button
    float minusBx = fieldX + 88.f;
    if (mx >= minusBx && mx < minusBx + 28 &&
        my >= sizeY && my < sizeY + kSetFieldH) {
        config_.font_size = (std::max)(6.f, config_.font_size - 1.f);
        notifySave();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Plus button
    float plusBx = fieldX + 120.f;
    if (mx >= plusBx && mx < plusBx + 28 &&
        my >= sizeY && my < sizeY + kSetFieldH) {
        config_.font_size = (std::min)(72.f, config_.font_size + 1.f);
        notifySave();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Font Features text field (row 2)
    float featY = fy + row * 2;
    if (mx >= fieldX && mx < fieldX + fieldW &&
        my >= featY && my < featY + kSetFieldH) {
        std::wstring featStr;
        for (size_t i = 0; i < config_.font_features.size(); ++i) {
            if (i > 0) featStr += L", ";
            featStr += toWide(config_.font_features[i]);
        }
        beginTextEdit(12, fieldX, featY, fieldW, featStr);
        return;
    }
}

// ---------------------------------------------------------------------------
// Keys tab click handling
// ---------------------------------------------------------------------------

void SettingsWindow::handleKeysClick(int mx, int my) {
    float fx = (float)kSetPadding;
    float fy = (float)(contentTop() + kSetPadding);
    int w = kSetWinWidth - 2 * kSetPadding;
    int h = kSetWinHeight - contentTop() - kSetPadding;
    float listTop = fy + 28.f;
    float listH = (float)h - 28.f - 40.f;
    float btnY = listTop + listH + 8.f;

    // Add button
    if (mx >= fx && mx < fx + 36 && my >= btnY && my < btnY + 28) {
        KeyBinding kb;
        kb.trigger = "ctrl+";
        kb.action = "none";
        config_.keybindings.push_back(kb);
        notifySave();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Remove button
    if (mx >= fx + 44 && mx < fx + 80 && my >= btnY && my < btnY + 28) {
        if (!config_.keybindings.empty()) {
            config_.keybindings.pop_back();
            notifySave();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    // Click on keybinding row fields
    for (size_t i = 0; i < keyBindingRows_.size(); ++i) {
        const auto& kbr = keyBindingRows_[i];
        if (mx >= kbr.triggerRect.left && mx < kbr.triggerRect.right &&
            my >= kbr.triggerRect.top && my < kbr.triggerRect.bottom) {
            // Edit trigger
            beginTextEdit(100 + (int)i * 2,
                          (float)kbr.triggerRect.left,
                          (float)kbr.triggerRect.top,
                          (float)(kbr.triggerRect.right - kbr.triggerRect.left),
                          toWide(config_.keybindings[i].trigger));
            return;
        }
        if (mx >= kbr.actionRect.left && mx < kbr.actionRect.right &&
            my >= kbr.actionRect.top && my < kbr.actionRect.bottom) {
            // Edit action
            beginTextEdit(101 + (int)i * 2,
                          (float)kbr.actionRect.left,
                          (float)kbr.actionRect.top,
                          (float)(kbr.actionRect.right - kbr.actionRect.left),
                          toWide(config_.keybindings[i].action));
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Clipboard tab click handling
// ---------------------------------------------------------------------------

void SettingsWindow::handleClipboardClick(int mx, int my) {
    float fx = (float)kSetPadding;
    float fy = (float)(contentTop() + kSetPadding);
    float fieldX = fx + kSetLabelW;
    float row = (float)kSetRowHeight;

    // Paste Protection pills (row 0)
    if (my >= fy && my < fy + kSetPillBtnH) {
        float pw = (float)kSetPillBtnW;
        float gap = (float)kSetPillGap;
        for (int i = 0; i < 3; ++i) {
            float px = fieldX + i * (pw + gap);
            if (mx >= px && mx < px + pw) {
                const char* vals[] = { "never", "multiline", "always" };
                config_.clipboard_paste_protection = vals[i];
                notifySave();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    }

    // Bracketed Paste Trust toggle (row 1, after description gap)
    float bracketY = fy + row + 44.f + 2.f;
    if (mx >= fieldX && mx < fieldX + kSetToggleW &&
        my >= bracketY && my < bracketY + kSetToggleH) {
        config_.clipboard_paste_bracketed_safe =
            !config_.clipboard_paste_bracketed_safe;
        notifySave();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    // Allow Clipboard Write toggle (row 2, after two description gaps)
    float allowY = fy + row * 2 + 88.f + 2.f;
    if (mx >= fieldX && mx < fieldX + kSetToggleW &&
        my >= allowY && my < allowY + kSetToggleH) {
        config_.allow_clipboard_write = !config_.allow_clipboard_write;
        notifySave();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

} // namespace termcore

#endif
