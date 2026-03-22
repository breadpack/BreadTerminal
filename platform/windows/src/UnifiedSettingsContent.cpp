#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"
#include "termcore/config_value_adapter.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "comdlg32.lib")

namespace termcore {

// ---------------------------------------------------------------------------
// paintContent
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintContent(Gdiplus::Graphics& g, int w, int h) {
    int contentLeft = sidebarWidth_ + kUsContentPad;
    int contentTop = kUsTopBarH + kUsContentPad;
    int contentW = w - sidebarWidth_ - kUsContentPad * 2;
    int contentH = h - kUsTopBarH - kUsBottomBarH - kUsContentPad * 2;

    // Section title
    const SettingsCategory* cat = model_ ? model_->category(selectedCategoryId_) : nullptr;
    std::wstring title = cat ? toWide(cat->label) : L"Settings";

    Gdiplus::Font titleFont(L"Segoe UI Semibold", 16.f);
    Gdiplus::SolidBrush textBr(toGdipColorCR(chrome_.textColor));
    Gdiplus::PointF titlePt((float)contentLeft, (float)contentTop - scrollY_);
    g.DrawString(title.c_str(), -1, &titleFont, titlePt, &textBr);

    int itemsY = contentTop + 40;
    int itemsX = contentLeft;

    if (!cat) return;

    switch (cat->sectionType) {
    case SectionType::CardGrid:
        if (cat->id.find("theme") != std::string::npos) {
            paintThemeCards(g, itemsX, itemsY, contentW, contentH);
        } else {
            paintFontCards(g, itemsX, itemsY, contentW, contentH);
        }
        break;
    case SectionType::KeybindingList:
        paintKeybindingList(g, itemsX, itemsY, contentW, contentH);
        break;
    case SectionType::Settings:
    default:
        paintSettingsItems(g, itemsX, itemsY, contentW, contentH);
        break;
    }
}

// ---------------------------------------------------------------------------
// paintSettingsItems
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintSettingsItems(Gdiplus::Graphics& g,
                                                int x, int y, int w, int h) {
    const SettingsCategory* cat = model_ ? model_->category(selectedCategoryId_) : nullptr;
    if (!cat || cat->items.empty()) {
        Gdiplus::Font font(L"Segoe UI", 10.f);
        Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText));
        Gdiplus::PointF pt((float)x, (float)y - scrollY_);
        g.DrawString(L"No settings in this section.", -1, &font, pt, &dimBr);
        return;
    }

    Gdiplus::Font labelFont(L"Segoe UI Semibold", 11.f);
    Gdiplus::Font descFont(L"Segoe UI", 9.f);
    Gdiplus::Font valueFont(L"Segoe UI", 10.f);
    Gdiplus::SolidBrush textBr(toGdipColorCR(chrome_.textColor));
    Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText));
    Gdiplus::SolidBrush accentBr(toGdipColorCR(chrome_.accent));
    Gdiplus::SolidBrush fieldBr(toGdipColorCR(chrome_.fieldBg));
    Gdiplus::SolidBrush btnBr(toGdipColorCR(chrome_.btnInactive));

    float curY = (float)y - scrollY_;
    constexpr float controlX = 320.f; // X offset from content left for controls

    for (const auto& item : cat->items) {
        float itemX = (float)x;
        float rowTop = curY;

        // Modified indicator: 3px blue vertical bar on left
        if (item.modified) {
            Gdiplus::SolidBrush modBr(Gdiplus::Color(255, 0, 122, 204)); // #007ACC
            g.FillRectangle(&modBr, itemX - 8.f, curY, 3.f, 40.f);
        }

        // Label
        Gdiplus::PointF labelPt(itemX, curY);
        std::wstring label = toWide(item.label);
        g.DrawString(label.c_str(), -1, &labelFont, labelPt, &textBr);

        // Description
        curY += 20.f;
        Gdiplus::PointF descPt(itemX, curY);
        std::wstring desc = toWide(item.description);
        g.DrawString(desc.c_str(), -1, &descFont, descPt, &dimBr);

        // Control widget - positioned to the right
        float ctrlX = itemX + controlX;
        float ctrlY = rowTop;

        switch (item.type) {
        case SettingType::Toggle: {
            // Toggle switch: 44x22, rounded track + circle knob
            bool val = getConfigBool(config_, item.key);
            float tw = 44.f, th = 22.f;
            float ty = ctrlY + 2.f;

            // Track
            if (val) {
                drawRoundedRect(g, &accentBr, ctrlX, ty, tw, th, th / 2.f);
            } else {
                drawRoundedRect(g, &btnBr, ctrlX, ty, tw, th, th / 2.f);
            }

            // Knob (white circle)
            Gdiplus::SolidBrush knobBr(Gdiplus::Color(255, 255, 255, 255));
            float knobR = 8.f;
            float knobX = val ? (ctrlX + tw - knobR * 2 - 3.f) : (ctrlX + 3.f);
            float knobY = ty + (th - knobR * 2) / 2.f;
            g.FillEllipse(&knobBr, knobX, knobY, knobR * 2, knobR * 2);
            break;
        }

        case SettingType::Text: {
            // Text field: 300x28, rounded rect
            std::string val = getConfigString(config_, item.key);
            float fw = 300.f, fh = 28.f;
            float fy = ctrlY;

            drawRoundedRect(g, &fieldBr, ctrlX, fy, fw, fh, 4.f);

            // Text value
            std::wstring wval = toWide(val.empty() ? "(default)" : val);
            Gdiplus::SolidBrush& valBr = val.empty() ? dimBr : textBr;
            Gdiplus::PointF valPt(ctrlX + 8.f, fy + 5.f);
            g.DrawString(wval.c_str(), -1, &valueFont, valPt, &valBr);
            break;
        }

        case SettingType::Number: {
            // Number field: 120x28 + stepper buttons (28x28 each)
            int val = getConfigInt(config_, item.key);
            float fw = 120.f, fh = 28.f, btnSz = 28.f;
            float fy = ctrlY;

            // Minus button
            drawRoundedRect(g, &btnBr, ctrlX, fy, btnSz, btnSz, 4.f);
            Gdiplus::PointF minusPt(ctrlX + 9.f, fy + 3.f);
            g.DrawString(L"\u2212", -1, &valueFont, minusPt, &textBr);

            // Number field
            float fieldX = ctrlX + btnSz + 2.f;
            drawRoundedRect(g, &fieldBr, fieldX, fy, fw, fh, 4.f);

            // Number value (centered)
            wchar_t numBuf[32];
            _snwprintf_s(numBuf, _countof(numBuf), L"%d", val);
            Gdiplus::RectF numRect(fieldX, fy, fw, fh);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(numBuf, -1, &valueFont, numRect, &fmt, &textBr);

            // Plus button
            float plusX = fieldX + fw + 2.f;
            drawRoundedRect(g, &btnBr, plusX, fy, btnSz, btnSz, 4.f);
            Gdiplus::PointF plusPt(plusX + 8.f, fy + 3.f);
            g.DrawString(L"+", -1, &valueFont, plusPt, &textBr);
            break;
        }

        case SettingType::Slider: {
            // Slider: 300px wide track (4px h) + filled portion + circle handle + value label
            float val = getConfigFloat(config_, item.key);
            float sW = 300.f, sH = 4.f;
            float sy = ctrlY + 12.f;
            float minV = item.meta.min;
            float maxV = item.meta.max;
            float range = maxV - minV;
            float ratio = (range > 0.f) ? (val - minV) / range : 0.f;
            ratio = (std::max)(0.f, (std::min)(1.f, ratio));

            // Track background
            drawRoundedRect(g, &btnBr, ctrlX, sy, sW, sH, 2.f);

            // Filled portion
            float filledW = sW * ratio;
            if (filledW > 0.f) {
                drawRoundedRect(g, &accentBr, ctrlX, sy, filledW, sH, 2.f);
            }

            // Handle (circle)
            float handleR = 7.f;
            float handleX = ctrlX + filledW - handleR;
            float handleY = sy + sH / 2.f - handleR;
            Gdiplus::SolidBrush handleBr(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&accentBr, handleX, handleY, handleR * 2, handleR * 2);
            g.FillEllipse(&handleBr, handleX + 2.f, handleY + 2.f,
                          handleR * 2 - 4.f, handleR * 2 - 4.f);

            // Value label
            wchar_t valBuf[32];
            _snwprintf_s(valBuf, _countof(valBuf), L"%.2f", val);
            Gdiplus::PointF valPt(ctrlX + sW + 12.f, ctrlY + 4.f);
            g.DrawString(valBuf, -1, &valueFont, valPt, &textBr);
            break;
        }

        case SettingType::Dropdown: {
            // Pill buttons: auto-width based on text, wrapping to next row if needed
            const auto& options = item.meta.options;
            std::string current = getConfigString(config_, item.key);

            // For background_blur, map int to string option
            if (item.key == "background_blur") {
                int iv = getConfigInt(config_, item.key);
                if (iv >= 0 && iv < (int)options.size()) {
                    current = options[iv];
                }
            }

            constexpr float pillH = 26.f, pillGap = 6.f, pillPadX = 16.f;
            constexpr float pillMinW = 52.f, rowGap = 4.f;
            float maxX = ctrlX + (float)w - 320.f; // available width for pills
            float px = ctrlX;
            float py = ctrlY;

            for (const auto& opt : options) {
                bool active = (opt == current);
                std::wstring wopt = toWide(opt);

                // Measure text width for auto-sizing
                Gdiplus::RectF bounds;
                g.MeasureString(wopt.c_str(), -1, &valueFont, Gdiplus::PointF(0, 0), &bounds);
                float pillW = (std::max)(pillMinW, bounds.Width + pillPadX);

                // Wrap to next row if needed
                if (px + pillW > maxX && px > ctrlX) {
                    px = ctrlX;
                    py += pillH + rowGap;
                }

                if (active) {
                    drawRoundedRect(g, &accentBr, px, py, pillW, pillH, pillH / 2.f);
                } else {
                    drawRoundedRect(g, &btnBr, px, py, pillW, pillH, pillH / 2.f);
                }

                // Pill label (centered)
                Gdiplus::RectF pillRect(px, py, pillW, pillH);
                Gdiplus::StringFormat fmt;
                fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                Gdiplus::SolidBrush pillTextBr(
                    active ? Gdiplus::Color(255, 255, 255, 255)
                           : toGdipColorCR(chrome_.textColor));
                g.DrawString(wopt.c_str(), -1, &valueFont, pillRect, &fmt, &pillTextBr);

                px += pillW + pillGap;
            }
            break;
        }

        case SettingType::ColorPicker: {
            // Color swatch: 28x28 rounded rect filled with color + hex label
            uint32_t val = getConfigColor(config_, item.key);
            float swSz = 28.f;

            Gdiplus::SolidBrush swatchBr(toGdipColor(val));
            drawRoundedRect(g, &swatchBr, ctrlX, ctrlY, swSz, swSz, 4.f);

            // 1px border around swatch
            Gdiplus::Pen borderPen(toGdipColorCR(chrome_.btnInactive), 1.f);
            Gdiplus::GraphicsPath borderPath;
            float bd = 8.f;
            borderPath.AddArc(ctrlX, ctrlY, bd, bd, 180.f, 90.f);
            borderPath.AddArc(ctrlX + swSz - bd, ctrlY, bd, bd, 270.f, 90.f);
            borderPath.AddArc(ctrlX + swSz - bd, ctrlY + swSz - bd, bd, bd, 0.f, 90.f);
            borderPath.AddArc(ctrlX, ctrlY + swSz - bd, bd, bd, 90.f, 90.f);
            borderPath.CloseFigure();
            g.DrawPath(&borderPen, &borderPath);

            // Hex label
            wchar_t hexBuf[16];
            _snwprintf_s(hexBuf, _countof(hexBuf), L"#%06X", val);
            Gdiplus::PointF hexPt(ctrlX + swSz + 8.f, ctrlY + 5.f);
            g.DrawString(hexBuf, -1, &valueFont, hexPt, &textBr);
            break;
        }
        }

        curY = rowTop + 52.f + (float)kUsItemSpacing;
    }
}

// ---------------------------------------------------------------------------
// onSettingsItemClick — handle clicks on setting controls
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onSettingsItemClick(int mx, int my) {
    const SettingsCategory* cat = model_ ? model_->category(selectedCategoryId_) : nullptr;
    if (!cat) return;

    int contentLeft = sidebarWidth_ + kUsContentPad;
    int contentTop = kUsTopBarH + kUsContentPad;
    float controlX = (float)contentLeft + 320.f;

    float curY = (float)(contentTop + 40) - scrollY_;

    for (const auto& item : cat->items) {
        float rowTop = curY;
        float ctrlX = controlX;
        float ctrlY = rowTop;

        switch (item.type) {
        case SettingType::Toggle: {
            float tw = 44.f, th = 22.f;
            float ty = ctrlY + 2.f;
            if ((float)mx >= ctrlX && (float)mx < ctrlX + tw &&
                (float)my >= ty && (float)my < ty + th) {
                bool val = getConfigBool(config_, item.key);
                setConfigBool(config_, item.key, !val);
                if (model_) model_->refreshModified(config_);
                notifySave();
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            break;
        }

        case SettingType::Text: {
            float fw = 300.f, fh = 28.f;
            float fy = ctrlY;
            if ((float)mx >= ctrlX && (float)mx < ctrlX + fw &&
                (float)my >= fy && (float)my < fy + fh) {
                std::string val = getConfigString(config_, item.key);
                beginInlineEdit(item.key, SettingType::Text,
                                ctrlX, fy, fw, fh, toWide(val));
                return;
            }
            break;
        }

        case SettingType::Number: {
            float btnSz = 28.f, fw = 120.f;
            float fy = ctrlY;
            // Minus button
            if ((float)mx >= ctrlX && (float)mx < ctrlX + btnSz &&
                (float)my >= fy && (float)my < fy + btnSz) {
                if (item.key == "font_size") {
                    float val = getConfigFloat(config_, item.key);
                    float newVal = (std::max)(item.meta.min, val - item.meta.step);
                    setConfigFloat(config_, item.key, newVal);
                } else {
                    int val = getConfigInt(config_, item.key);
                    int newVal = val - (int)item.meta.step;
                    if (item.meta.min != item.meta.max)
                        newVal = (std::max)((int)item.meta.min, newVal);
                    setConfigInt(config_, item.key, newVal);
                }
                if (model_) model_->refreshModified(config_);
                notifySave();
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            // Plus button
            float plusX = ctrlX + btnSz + 2.f + fw + 2.f;
            if ((float)mx >= plusX && (float)mx < plusX + btnSz &&
                (float)my >= fy && (float)my < fy + btnSz) {
                if (item.key == "font_size") {
                    float val = getConfigFloat(config_, item.key);
                    float newVal = (std::min)(item.meta.max, val + item.meta.step);
                    setConfigFloat(config_, item.key, newVal);
                } else {
                    int val = getConfigInt(config_, item.key);
                    int newVal = val + (int)item.meta.step;
                    if (item.meta.min != item.meta.max)
                        newVal = (std::min)((int)item.meta.max, newVal);
                    setConfigInt(config_, item.key, newVal);
                }
                if (model_) model_->refreshModified(config_);
                notifySave();
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            // Click on the number field itself -> inline edit
            float fieldX = ctrlX + btnSz + 2.f;
            if ((float)mx >= fieldX && (float)mx < fieldX + fw &&
                (float)my >= fy && (float)my < fy + 28.f) {
                wchar_t buf[32];
                if (item.key == "font_size") {
                    _snwprintf_s(buf, _countof(buf), L"%.1f", getConfigFloat(config_, item.key));
                } else {
                    _snwprintf_s(buf, _countof(buf), L"%d", getConfigInt(config_, item.key));
                }
                beginInlineEdit(item.key, SettingType::Number,
                                fieldX, fy, fw, 28.f, buf);
                return;
            }
            break;
        }

        case SettingType::Slider: {
            float sW = 300.f;
            float sy = ctrlY + 5.f;
            float sH = 22.f;
            if ((float)mx >= ctrlX && (float)mx < ctrlX + sW &&
                (float)my >= sy && (float)my < sy + sH) {
                float ratio = ((float)mx - ctrlX) / sW;
                ratio = (std::max)(0.f, (std::min)(1.f, ratio));
                float newVal = item.meta.min + ratio * (item.meta.max - item.meta.min);
                // Snap to step
                if (item.meta.step > 0.f) {
                    newVal = std::round(newVal / item.meta.step) * item.meta.step;
                }
                newVal = (std::max)(item.meta.min, (std::min)(item.meta.max, newVal));
                setConfigFloat(config_, item.key, newVal);
                if (model_) model_->refreshModified(config_);
                notifySave();
                if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
            break;
        }

        case SettingType::Dropdown: {
            const auto& options = item.meta.options;
            constexpr float pillH = 26.f, pillGap = 6.f, pillPadX = 16.f;
            constexpr float pillMinW = 52.f, rowGap = 4.f;
            RECT clientRc;
            GetClientRect(hwnd_, &clientRc);
            float maxX = ctrlX + (float)(clientRc.right - clientRc.left) - sidebarWidth_ - kUsContentPad * 2 - 320.f;
            // Estimate pill widths using approximate character width (7px per char)
            float px = ctrlX;
            float py = ctrlY;
            for (int i = 0; i < (int)options.size(); ++i) {
                float textW = (float)options[i].size() * 7.f;
                float pillW = (std::max)(pillMinW, textW + pillPadX);
                // Wrap to next row if needed
                if (px + pillW > maxX && px > ctrlX) {
                    px = ctrlX;
                    py += pillH + rowGap;
                }
                if ((float)mx >= px && (float)mx < px + pillW &&
                    (float)my >= py && (float)my < py + pillH) {
                    if (item.key == "background_blur") {
                        setConfigInt(config_, item.key, i);
                    } else {
                        setConfigString(config_, item.key, options[i]);
                    }
                    if (model_) model_->refreshModified(config_);
                    notifySave();
                    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
                px += pillW + pillGap;
            }
            break;
        }

        case SettingType::ColorPicker: {
            float swSz = 28.f;
            if ((float)mx >= ctrlX && (float)mx < ctrlX + swSz &&
                (float)my >= ctrlY && (float)my < ctrlY + swSz) {
                // Open native color picker dialog
                uint32_t currentColor = getConfigColor(config_, item.key);
                COLORREF customColors[16] = {};
                CHOOSECOLOR cc = {};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd_;
                cc.rgbResult = RGB((currentColor >> 16) & 0xFF,
                                   (currentColor >> 8) & 0xFF,
                                   currentColor & 0xFF);
                cc.lpCustColors = customColors;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;

                if (ChooseColor(&cc)) {
                    uint32_t newColor = ((uint32_t)GetRValue(cc.rgbResult) << 16)
                                      | ((uint32_t)GetGValue(cc.rgbResult) << 8)
                                      |  (uint32_t)GetBValue(cc.rgbResult);
                    setConfigColor(config_, item.key, newColor);

                    chrome_ = deriveChrome(config_.background, config_.foreground, config_.palette);
                    if (model_) model_->refreshModified(config_);
                    notifySave();
                    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return;
            }
            break;
        }

        default:
            break;
        }

        curY = rowTop + 52.f + (float)kUsItemSpacing;
    }
}

// ---------------------------------------------------------------------------
// Inline edit: create a temporary EDIT control over a text/number field
// ---------------------------------------------------------------------------

static constexpr int kInlineEditId = 1002;

void UnifiedSettingsWindow::beginInlineEdit(const std::string& key,
                                             SettingType type,
                                             float x, float y, float w, float h,
                                             const std::wstring& currentValue) {
    // Destroy any existing inline edit
    cancelInlineEdit();

    inlineEditKey_ = key;
    inlineEditType_ = type;
    inlineEditRect_ = { (int)x, (int)y, (int)(x + w), (int)(y + h) };

    DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER;
    if (type == SettingType::Number) {
        style |= ES_NUMBER;
    }

    inlineEdit_ = CreateWindowExW(
        0, L"EDIT", currentValue.c_str(),
        style,
        (int)x, (int)y, (int)w, (int)h,
        hwnd_, (HMENU)(INT_PTR)kInlineEditId,
        (HINSTANCE)GetWindowLongPtr(hwnd_, GWLP_HINSTANCE), nullptr);

    if (inlineEdit_) {
        // Set font
        HFONT hFont = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessageW(inlineEdit_, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Select all text
        SendMessageW(inlineEdit_, EM_SETSEL, 0, -1);
        SetFocus(inlineEdit_);
    }
}

void UnifiedSettingsWindow::commitInlineEdit() {
    if (!inlineEdit_) return;

    int len = GetWindowTextLengthW(inlineEdit_);
    std::wstring wtext(len + 1, L'\0');
    GetWindowTextW(inlineEdit_, wtext.data(), len + 1);
    wtext.resize(len);

    // Convert to UTF-8
    std::string text;
    if (!wtext.empty()) {
        int sz = WideCharToMultiByte(CP_UTF8, 0, wtext.c_str(), (int)wtext.size(),
                                      nullptr, 0, nullptr, nullptr);
        text.resize(sz);
        WideCharToMultiByte(CP_UTF8, 0, wtext.c_str(), (int)wtext.size(),
                            text.data(), sz, nullptr, nullptr);
    }

    if (inlineEditType_ == SettingType::Text) {
        setConfigString(config_, inlineEditKey_, text);
    } else if (inlineEditType_ == SettingType::Number) {
        // Try float first (for font_size), then int
        if (inlineEditKey_ == "font_size") {
            try {
                float val = std::stof(text);
                setConfigFloat(config_, inlineEditKey_, val);
            } catch (...) {}
        } else {
            try {
                int val = std::stoi(text);
                setConfigInt(config_, inlineEditKey_, val);
            } catch (...) {}
        }
    }

    cancelInlineEdit();

    if (model_) model_->refreshModified(config_);
    notifySave();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void UnifiedSettingsWindow::cancelInlineEdit() {
    if (inlineEdit_) {
        DestroyWindow(inlineEdit_);
        inlineEdit_ = nullptr;
    }
    inlineEditKey_.clear();
}

} // namespace termcore

#endif
