#if defined(__linux__)

#include "UnifiedSettingsWindow.h"
#include "termcore/config_value_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace termcore {

// ---------------------------------------------------------------------------
// buildContentArea
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::buildContentArea() {
    contentScroll_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(contentScroll_),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_add_css_class(contentScroll_, "content-area");

    contentBox_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(contentBox_, kUsContentPad);
    gtk_widget_set_margin_end(contentBox_, kUsContentPad);
    gtk_widget_set_margin_top(contentBox_, kUsContentPad);
    gtk_widget_set_margin_bottom(contentBox_, kUsContentPad);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(contentScroll_), contentBox_);
    gtk_paned_set_end_child(GTK_PANED(paned_), contentScroll_);
}

// ---------------------------------------------------------------------------
// clearContent
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::clearContent() {
    if (!contentBox_) return;

    GtkWidget* child = gtk_widget_get_first_child(contentBox_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(contentBox_), child);
        child = next;
    }
}

// ---------------------------------------------------------------------------
// showCategoryContent
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::showCategoryContent(const std::string& categoryId) {
    clearContent();

    if (!model_) return;

    const SettingsCategory* cat = model_->category(categoryId);
    if (!cat) return;

    // Section title
    GtkWidget* title = gtk_label_new(cat->label.c_str());
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_widget_add_css_class(title, "section-title");
    gtk_box_append(GTK_BOX(contentBox_), title);

    switch (cat->sectionType) {
    case SectionType::CardGrid:
        if (cat->id.find("theme") != std::string::npos) {
            showThemeCards();
        } else {
            showFontCards();
        }
        break;

    case SectionType::KeybindingList:
        showKeybindingList();
        break;

    case SectionType::Settings:
    default:
        showSettingsItems(cat);
        break;
    }

    // Scroll to top
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(contentScroll_));
    if (adj) gtk_adjustment_set_value(adj, 0.0);
}

// ---------------------------------------------------------------------------
// showSettingsItems
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::showSettingsItems(const SettingsCategory* cat) {
    if (!cat || cat->items.empty()) {
        GtkWidget* label = gtk_label_new("No settings in this section.");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_add_css_class(label, "setting-description");
        gtk_box_append(GTK_BOX(contentBox_), label);
        return;
    }

    for (const auto& item : cat->items) {
        // Item container: horizontal box with optional modified indicator
        GtkWidget* itemBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_bottom(itemBox, kUsItemSpacing);

        // Modified indicator (3px blue bar on left)
        if (item.modified) {
            GtkWidget* indicator = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_widget_add_css_class(indicator, "modified-indicator");
            gtk_widget_set_size_request(indicator, 3, -1);
            gtk_box_append(GTK_BOX(itemBox), indicator);
        } else {
            // 3px spacer for alignment
            GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            gtk_widget_set_size_request(spacer, 3, -1);
            gtk_box_append(GTK_BOX(itemBox), spacer);
        }

        // Left side: label + description
        GtkWidget* labelBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(labelBox, TRUE);
        gtk_widget_set_size_request(labelBox, 200, -1);

        GtkWidget* labelWidget = gtk_label_new(item.label.c_str());
        gtk_label_set_xalign(GTK_LABEL(labelWidget), 0.0);
        gtk_widget_add_css_class(labelWidget, "setting-label");
        gtk_box_append(GTK_BOX(labelBox), labelWidget);

        if (!item.description.empty()) {
            GtkWidget* descWidget = gtk_label_new(item.description.c_str());
            gtk_label_set_xalign(GTK_LABEL(descWidget), 0.0);
            gtk_label_set_wrap(GTK_LABEL(descWidget), TRUE);
            gtk_widget_add_css_class(descWidget, "setting-description");
            gtk_box_append(GTK_BOX(labelBox), descWidget);
        }

        gtk_box_append(GTK_BOX(itemBox), labelBox);

        // Right side: control widget
        GtkWidget* control = nullptr;

        switch (item.type) {
        case SettingType::Toggle: {
            bool val = getConfigBool(config_, item.key);
            GtkWidget* sw = gtk_switch_new();
            gtk_switch_set_active(GTK_SWITCH(sw), val);
            gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);

            // Capture key for callback
            std::string* keyPtr = new std::string(item.key);
            g_signal_connect(sw, "state-set",
                G_CALLBACK(+[](GtkSwitch* sw, gboolean state, gpointer data) -> gboolean {
                    auto* keyPtr = static_cast<std::string*>(data);
                    // The UnifiedSettingsWindow* is stored as object data
                    auto* self = static_cast<UnifiedSettingsWindow*>(
                        g_object_get_data(G_OBJECT(sw), "settings-window"));
                    if (self) {
                        setConfigBool(self->config_, *keyPtr, state);
                        self->notifySave();
                    }
                    return FALSE;
                }), keyPtr);
            g_object_set_data(G_OBJECT(sw), "settings-window", this);
            g_signal_connect(sw, "destroy",
                G_CALLBACK(+[](GtkWidget*, gpointer data) {
                    delete static_cast<std::string*>(data);
                }), keyPtr);

            control = sw;
            break;
        }

        case SettingType::Text: {
            std::string val = getConfigString(config_, item.key);
            GtkWidget* entry = gtk_entry_new();
            gtk_editable_set_text(GTK_EDITABLE(entry), val.c_str());
            gtk_widget_set_size_request(entry, 250, -1);
            gtk_widget_set_valign(entry, GTK_ALIGN_CENTER);

            std::string* keyPtr = new std::string(item.key);
            g_signal_connect(entry, "changed",
                G_CALLBACK(+[](GtkEditable* editable, gpointer data) {
                    auto* keyPtr = static_cast<std::string*>(data);
                    auto* self = static_cast<UnifiedSettingsWindow*>(
                        g_object_get_data(G_OBJECT(editable), "settings-window"));
                    if (self) {
                        const char* text = gtk_editable_get_text(editable);
                        setConfigString(self->config_, *keyPtr, text ? text : "");
                        self->notifySave();
                    }
                }), keyPtr);
            g_object_set_data(G_OBJECT(entry), "settings-window", this);
            g_signal_connect(entry, "destroy",
                G_CALLBACK(+[](GtkWidget*, gpointer data) {
                    delete static_cast<std::string*>(data);
                }), keyPtr);

            control = entry;
            break;
        }

        case SettingType::Number: {
            double val;
            double minV = item.meta.min;
            double maxV = item.meta.max;
            double step = item.meta.step;

            if (item.key == "font_size") {
                val = getConfigFloat(config_, item.key);
            } else {
                val = getConfigInt(config_, item.key);
            }

            if (maxV <= minV) { minV = 0; maxV = 100000; }
            if (step <= 0) step = 1;

            GtkWidget* spin = gtk_spin_button_new_with_range(minV, maxV, step);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), val);
            if (item.key == "font_size") {
                gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
            }
            gtk_widget_set_size_request(spin, 120, -1);
            gtk_widget_set_valign(spin, GTK_ALIGN_CENTER);

            std::string* keyPtr = new std::string(item.key);
            g_signal_connect(spin, "value-changed",
                G_CALLBACK(+[](GtkSpinButton* btn, gpointer data) {
                    auto* keyPtr = static_cast<std::string*>(data);
                    auto* self = static_cast<UnifiedSettingsWindow*>(
                        g_object_get_data(G_OBJECT(btn), "settings-window"));
                    if (self) {
                        double v = gtk_spin_button_get_value(btn);
                        if (*keyPtr == "font_size") {
                            setConfigFloat(self->config_, *keyPtr, (float)v);
                        } else {
                            setConfigInt(self->config_, *keyPtr, (int)v);
                        }
                        self->notifySave();
                    }
                }), keyPtr);
            g_object_set_data(G_OBJECT(spin), "settings-window", this);
            g_signal_connect(spin, "destroy",
                G_CALLBACK(+[](GtkWidget*, gpointer data) {
                    delete static_cast<std::string*>(data);
                }), keyPtr);

            control = spin;
            break;
        }

        case SettingType::Slider: {
            float val = getConfigFloat(config_, item.key);
            double minV = item.meta.min;
            double maxV = item.meta.max;
            double step = item.meta.step;
            if (maxV <= minV) { minV = 0.0; maxV = 1.0; }
            if (step <= 0) step = 0.01;

            GtkWidget* scaleBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

            GtkWidget* scale = gtk_scale_new_with_range(
                GTK_ORIENTATION_HORIZONTAL, minV, maxV, step);
            gtk_range_set_value(GTK_RANGE(scale), val);
            gtk_widget_set_size_request(scale, 250, -1);
            gtk_widget_set_hexpand(scale, FALSE);

            // Value label
            char valBuf[32];
            snprintf(valBuf, sizeof(valBuf), "%.2f", val);
            GtkWidget* valLabel = gtk_label_new(valBuf);
            gtk_widget_set_size_request(valLabel, 50, -1);

            struct SliderData {
                std::string key;
                GtkWidget* valueLabel;
            };
            auto* sd = new SliderData{item.key, valLabel};

            g_signal_connect(scale, "value-changed",
                G_CALLBACK(+[](GtkRange* range, gpointer data) {
                    auto* sd = static_cast<SliderData*>(data);
                    auto* self = static_cast<UnifiedSettingsWindow*>(
                        g_object_get_data(G_OBJECT(range), "settings-window"));
                    if (self) {
                        double v = gtk_range_get_value(range);
                        setConfigFloat(self->config_, sd->key, (float)v);
                        self->notifySave();

                        char buf[32];
                        snprintf(buf, sizeof(buf), "%.2f", v);
                        gtk_label_set_text(GTK_LABEL(sd->valueLabel), buf);
                    }
                }), sd);
            g_object_set_data(G_OBJECT(scale), "settings-window", this);
            g_signal_connect(scale, "destroy",
                G_CALLBACK(+[](GtkWidget*, gpointer data) {
                    delete static_cast<SliderData*>(data);
                }), sd);

            gtk_box_append(GTK_BOX(scaleBox), scale);
            gtk_box_append(GTK_BOX(scaleBox), valLabel);
            gtk_widget_set_valign(scaleBox, GTK_ALIGN_CENTER);

            control = scaleBox;
            break;
        }

        case SettingType::Dropdown: {
            const auto& options = item.meta.options;
            std::string current;

            if (item.key == "background_blur") {
                int iv = getConfigInt(config_, item.key);
                if (iv >= 0 && iv < (int)options.size()) {
                    current = options[iv];
                }
            } else {
                current = getConfigString(config_, item.key);
            }

            GtkWidget* combo = gtk_combo_box_text_new();
            int activeIdx = 0;
            for (int i = 0; i < (int)options.size(); ++i) {
                gtk_combo_box_text_append_text(
                    GTK_COMBO_BOX_TEXT(combo), options[i].c_str());
                if (options[i] == current) activeIdx = i;
            }
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), activeIdx);
            gtk_widget_set_valign(combo, GTK_ALIGN_CENTER);

            struct DropdownData {
                std::string key;
                std::vector<std::string> options;
            };
            auto* dd = new DropdownData{item.key, options};

            g_signal_connect(combo, "changed",
                G_CALLBACK(+[](GtkComboBox* box, gpointer data) {
                    auto* dd = static_cast<DropdownData*>(data);
                    auto* self = static_cast<UnifiedSettingsWindow*>(
                        g_object_get_data(G_OBJECT(box), "settings-window"));
                    if (self) {
                        int idx = gtk_combo_box_get_active(box);
                        if (idx >= 0 && idx < (int)dd->options.size()) {
                            if (dd->key == "background_blur") {
                                setConfigInt(self->config_, dd->key, idx);
                            } else {
                                setConfigString(self->config_, dd->key,
                                               dd->options[idx]);
                            }
                            self->notifySave();
                        }
                    }
                }), dd);
            g_object_set_data(G_OBJECT(combo), "settings-window", this);
            g_signal_connect(combo, "destroy",
                G_CALLBACK(+[](GtkWidget*, gpointer data) {
                    delete static_cast<DropdownData*>(data);
                }), dd);

            control = combo;
            break;
        }

        case SettingType::ColorPicker: {
            uint32_t val = getConfigColor(config_, item.key);
            GdkRGBA rgba;
            rgba.red   = ((val >> 16) & 0xFF) / 255.0f;
            rgba.green = ((val >> 8) & 0xFF) / 255.0f;
            rgba.blue  = (val & 0xFF) / 255.0f;
            rgba.alpha = 1.0f;

            GtkWidget* colorBtn = gtk_color_button_new_with_rgba(&rgba);
            gtk_widget_set_valign(colorBtn, GTK_ALIGN_CENTER);

            std::string* keyPtr = new std::string(item.key);
            g_signal_connect(colorBtn, "color-set",
                G_CALLBACK(+[](GtkColorButton* btn, gpointer data) {
                    auto* keyPtr = static_cast<std::string*>(data);
                    auto* self = static_cast<UnifiedSettingsWindow*>(
                        g_object_get_data(G_OBJECT(btn), "settings-window"));
                    if (self) {
                        GdkRGBA rgba;
                        gtk_color_chooser_get_rgba(
                            GTK_COLOR_CHOOSER(btn), &rgba);
                        uint32_t color =
                            ((uint32_t)(rgba.red * 255) << 16) |
                            ((uint32_t)(rgba.green * 255) << 8) |
                            (uint32_t)(rgba.blue * 255);
                        setConfigColor(self->config_, *keyPtr, color);
                        self->notifySave();
                    }
                }), keyPtr);
            g_object_set_data(G_OBJECT(colorBtn), "settings-window", this);
            g_signal_connect(colorBtn, "destroy",
                G_CALLBACK(+[](GtkWidget*, gpointer data) {
                    delete static_cast<std::string*>(data);
                }), keyPtr);

            control = colorBtn;
            break;
        }
        } // switch

        if (control) {
            gtk_box_append(GTK_BOX(itemBox), control);
        }

        gtk_box_append(GTK_BOX(contentBox_), itemBox);
    }
}

} // namespace termcore

#endif // __linux__
