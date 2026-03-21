#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace termcore {

// ---------------------------------------------------------------------------
// Config value helpers (read config values by key for rendering)
// ---------------------------------------------------------------------------

static std::string getStringValue(const Config& cfg, const std::string& key) {
    if (key == "shell") return cfg.shell;
    if (key == "cursor_style") return cfg.cursor_style;
    if (key == "clipboard_paste_protection") return cfg.clipboard_paste_protection;
    if (key == "font_family") return cfg.font_family;
    if (key == "theme") return cfg.theme;
    return {};
}

static float getFloatValue(const Config& cfg, const std::string& key) {
    if (key == "font_size") return cfg.font_size;
    if (key == "background_opacity") return cfg.background_opacity;
    if (key == "cursor_blink_interval") return cfg.cursor_blink_interval;
    if (key == "minimum_contrast") return cfg.minimum_contrast;
    return 0.0f;
}

static int getIntValue(const Config& cfg, const std::string& key) {
    if (key == "window_width") return cfg.window_width;
    if (key == "window_height") return cfg.window_height;
    if (key == "window_padding") return cfg.window_padding;
    if (key == "scrollback_limit") return cfg.scrollback_limit;
    if (key == "background_blur") return cfg.background_blur;
    return 0;
}

static bool getBoolValue(const Config& cfg, const std::string& key) {
    if (key == "cursor_blink") return cfg.cursor_blink;
    if (key == "clipboard_paste_bracketed_safe") return cfg.clipboard_paste_bracketed_safe;
    if (key == "allow_clipboard_write") return cfg.allow_clipboard_write;
    return false;
}

static uint32_t getColorValue(const Config& cfg, const std::string& key) {
    if (key == "background") return cfg.background;
    if (key == "foreground") return cfg.foreground;
    if (key == "cursor_color") return cfg.cursor_color;
    if (key == "selection_background") return cfg.selection_background;
    if (key == "selection_foreground") return cfg.selection_foreground;
    return 0;
}

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
            bool val = getBoolValue(config_, item.key);
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
            std::string val = getStringValue(config_, item.key);
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
            int val = getIntValue(config_, item.key);
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
            float val = getFloatValue(config_, item.key);
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
            // Pill buttons: horizontal row of 72x26 rounded pills
            const auto& options = item.meta.options;
            std::string current = getStringValue(config_, item.key);

            // For background_blur, map int to string option
            if (item.key == "background_blur") {
                int iv = getIntValue(config_, item.key);
                if (iv >= 0 && iv < (int)options.size()) {
                    current = options[iv];
                }
            }

            float pillW = 72.f, pillH = 26.f, pillGap = 6.f;
            float px = ctrlX;

            for (const auto& opt : options) {
                bool active = (opt == current);

                if (active) {
                    drawRoundedRect(g, &accentBr, px, ctrlY, pillW, pillH, pillH / 2.f);
                } else {
                    drawRoundedRect(g, &btnBr, px, ctrlY, pillW, pillH, pillH / 2.f);
                }

                // Pill label (centered)
                std::wstring wopt = toWide(opt);
                Gdiplus::RectF pillRect(px, ctrlY, pillW, pillH);
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
            uint32_t val = getColorValue(config_, item.key);
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

} // namespace termcore

#endif
