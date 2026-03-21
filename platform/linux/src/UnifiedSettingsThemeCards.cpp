#if defined(__linux__)

#include "UnifiedSettingsWindow.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace termcore {

// ---------------------------------------------------------------------------
// Theme card draw data (passed to Cairo draw function)
// ---------------------------------------------------------------------------

struct ThemeCardDrawData {
    UnifiedSettingsWindow* window;
    const ThemeMetadata* meta;
    bool isActive;
    uint32_t configBg;
    uint32_t configFg;
};

// ---------------------------------------------------------------------------
// rebuildThemeFilteredList
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::rebuildThemeFilteredList() {
    filteredThemes_.clear();

    bool dark  = (activeThemeFilter_ == ThemeFilter::Dark);
    bool light = (activeThemeFilter_ == ThemeFilter::Light);
    bool inst  = (activeThemeFilter_ == ThemeFilter::Installed);

    if (dark || light || inst) {
        filteredThemes_ = themeIndex_.filterByCategory(dark, light, inst);
    } else {
        for (auto& t : themeIndex_.all())
            filteredThemes_.push_back(&t);
    }
}

// ---------------------------------------------------------------------------
// showThemeCards
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::showThemeCards() {
    if (filteredThemes_.empty() && themeIndex_.count() > 0) {
        rebuildThemeFilteredList();
    }

    // --- Filter bar ---
    GtkWidget* filterBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, kTcFilterGap);
    gtk_widget_set_margin_bottom(filterBox, 12);

    static const char* filterLabels[] = {"All", "Dark", "Light", "Installed"};

    for (int i = 0; i < 4; ++i) {
        GtkWidget* btn = gtk_toggle_button_new_with_label(filterLabels[i]);
        gtk_widget_add_css_class(btn, "filter-button");
        if (i == static_cast<int>(activeThemeFilter_)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);
            gtk_widget_add_css_class(btn, "active");
        }

        int* idxPtr = new int(i);
        g_signal_connect(btn, "toggled",
            G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
                int idx = *static_cast<int*>(data);
                auto* self = static_cast<UnifiedSettingsWindow*>(
                    g_object_get_data(G_OBJECT(btn), "settings-window"));
                if (self && gtk_toggle_button_get_active(btn)) {
                    self->onThemeFilterChanged(idx);
                }
            }), idxPtr);
        g_object_set_data(G_OBJECT(btn), "settings-window", this);
        g_signal_connect(btn, "destroy",
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                delete static_cast<int*>(data);
            }), idxPtr);

        gtk_box_append(GTK_BOX(filterBox), btn);
    }

    gtk_box_append(GTK_BOX(contentBox_), filterBox);

    // --- Card grid using GtkFlowBox ---
    GtkWidget* flowBox = gtk_flow_box_new();
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flowBox), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flowBox), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flowBox), 10);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flowBox), kTcCardGap);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flowBox), kTcCardGap);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flowBox), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(flowBox, "card-grid");

    auto iequal = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t j = 0; j < a.size(); ++j)
            if (tolower((unsigned char)a[j]) != tolower((unsigned char)b[j]))
                return false;
        return true;
    };

    for (size_t i = 0; i < filteredThemes_.size(); ++i) {
        const ThemeMetadata* meta = filteredThemes_[i];

        bool isActive = false;
        if (!config_.theme.empty()) {
            isActive = iequal(meta->name, config_.theme)
                    || config_.theme.find(meta->name) != std::string::npos;
        } else {
            isActive = (meta->background == config_.background
                     && meta->foreground == config_.foreground);
        }

        // Card widget: GtkDrawingArea for Cairo custom rendering
        GtkWidget* cardArea = gtk_drawing_area_new();
        gtk_widget_set_size_request(cardArea, kTcCardW, kTcCardH);

        auto* drawData = new ThemeCardDrawData{
            this, meta, isActive, config_.background, config_.foreground};

        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(cardArea),
            UnifiedSettingsWindow::drawThemeCard, drawData, nullptr);

        g_signal_connect(cardArea, "destroy",
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                delete static_cast<ThemeCardDrawData*>(data);
            }), drawData);

        // Wrap in a clickable button-like container
        GtkWidget* cardBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(cardBox), cardArea);

        // "Apply" / "Applied" button below the card
        GtkWidget* applyBtn;
        if (isActive) {
            applyBtn = gtk_button_new_with_label("Applied");
            gtk_widget_set_sensitive(applyBtn, FALSE);
        } else {
            applyBtn = gtk_button_new_with_label("Apply");
        }
        gtk_widget_set_halign(applyBtn, GTK_ALIGN_END);
        gtk_widget_set_margin_top(applyBtn, 4);

        int* cardIdx = new int(static_cast<int>(i));
        g_signal_connect(applyBtn, "clicked",
            G_CALLBACK(+[](GtkButton*, gpointer data) {
                int idx = *static_cast<int*>(data);
                // We need to get 'self' from parent chain; store it on the button
                // This is handled via the flow box rebuild approach
            }), cardIdx);

        // Store self pointer on the button for the callback
        g_object_set_data(G_OBJECT(applyBtn), "settings-window", this);
        g_object_set_data(G_OBJECT(applyBtn), "card-index", cardIdx);

        // Replace the simple clicked handler with one that can access self
        g_signal_handlers_disconnect_by_data(applyBtn, cardIdx);
        g_signal_connect(applyBtn, "clicked",
            G_CALLBACK(+[](GtkButton* btn, gpointer) {
                auto* self = static_cast<UnifiedSettingsWindow*>(
                    g_object_get_data(G_OBJECT(btn), "settings-window"));
                auto* idxPtr = static_cast<int*>(
                    g_object_get_data(G_OBJECT(btn), "card-index"));
                if (self && idxPtr) {
                    self->onThemeCardApply(*idxPtr);
                }
            }), nullptr);

        g_signal_connect(applyBtn, "destroy",
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                delete static_cast<int*>(data);
            }), cardIdx);

        // Theme name label
        GtkWidget* nameLabel = gtk_label_new(meta->name.c_str());
        gtk_label_set_xalign(GTK_LABEL(nameLabel), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(nameLabel), PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_top(nameLabel, 4);

        // Bottom row: name + button
        GtkWidget* bottomRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_hexpand(nameLabel, TRUE);
        gtk_box_append(GTK_BOX(bottomRow), nameLabel);
        gtk_box_append(GTK_BOX(bottomRow), applyBtn);

        gtk_box_append(GTK_BOX(cardBox), bottomRow);

        gtk_flow_box_append(GTK_FLOW_BOX(flowBox), cardBox);
    }

    gtk_box_append(GTK_BOX(contentBox_), flowBox);

    // "No themes found" message
    if (filteredThemes_.empty()) {
        GtkWidget* emptyLabel = gtk_label_new("No themes found");
        gtk_widget_add_css_class(emptyLabel, "setting-description");
        gtk_box_append(GTK_BOX(contentBox_), emptyLabel);
    }
}

// ---------------------------------------------------------------------------
// drawThemeCard — Cairo draw function for a single theme card
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::drawThemeCard(GtkDrawingArea* /*area*/,
                                            cairo_t* cr,
                                            int width, int height,
                                            gpointer data) {
    auto* d = static_cast<ThemeCardDrawData*>(data);
    if (!d || !d->meta) return;

    double w = width;
    double h = height;
    double r = 8.0;

    // Rounded rect path helper
    auto roundedRect = [](cairo_t* cr, double x, double y,
                          double w, double h, double r) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
        cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
        cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr);
    };

    // Card background
    roundedRect(cr, 0, 0, w, h, r);
    cairo_set_source_rgba(cr, 0.15, 0.15, 0.2, 1.0);
    cairo_fill(cr);

    // Top preview area (60px): fill with theme background
    double previewH = 60.0;
    cairo_save(cr);
    roundedRect(cr, 0, 0, w, previewH + r, r);
    cairo_rectangle(cr, 0, 0, w, previewH);
    cairo_clip(cr);

    setCairoColor(cr, d->meta->background);
    cairo_paint(cr);

    // Sample terminal text
    cairo_select_font_face(cr, "monospace",
                           CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);

    setCairoColor(cr, d->meta->foreground);
    cairo_move_to(cr, 8, 18);
    cairo_show_text(cr, "$ hello world");
    cairo_move_to(cr, 8, 34);
    cairo_show_text(cr, "> echo $PATH");

    // Colorful line using palette
    setCairoColor(cr, d->meta->palette[1]); // red
    cairo_move_to(cr, 8, 50);
    cairo_show_text(cr, "err");
    setCairoColor(cr, d->meta->palette[2]); // green
    cairo_move_to(cr, 34, 50);
    cairo_show_text(cr, " ok");
    setCairoColor(cr, d->meta->palette[4]); // blue
    cairo_move_to(cr, 58, 50);
    cairo_show_text(cr, " info");

    cairo_restore(cr);

    // Palette swatches (2 rows of 8)
    double sx0 = 8;
    double sy0 = previewH + 4;
    for (int i = 0; i < 16; ++i) {
        int col = i % 8;
        int row = i / 8;
        double sx = sx0 + col * (kTcSwatchSize + kTcSwatchGap);
        double sy = sy0 + row * (kTcSwatchSize + kTcSwatchGap);

        roundedRect(cr, sx, sy, kTcSwatchSize, kTcSwatchSize, 2.0);
        setCairoColor(cr, d->meta->palette[i]);
        cairo_fill(cr);
    }
}

// ---------------------------------------------------------------------------
// onThemeFilterChanged
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onThemeFilterChanged(int filterIndex) {
    activeThemeFilter_ = static_cast<ThemeFilter>(filterIndex);
    rebuildThemeFilteredList();

    // Rebuild the content area
    showCategoryContent(selectedCategoryId_);
}

// ---------------------------------------------------------------------------
// onThemeCardApply
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onThemeCardApply(int idx) {
    if (idx < 0 || idx >= (int)filteredThemes_.size()) return;
    const ThemeMetadata* meta = filteredThemes_[idx];
    if (!meta) return;

    // Update config with theme colors
    config_.theme = meta->name;
    config_.background = meta->background;
    config_.foreground = meta->foreground;
    for (int i = 0; i < 16; ++i)
        config_.palette[i] = meta->palette[i];

    notifySave();

    // Re-apply CSS with new colors and rebuild content
    if (cssProvider_) {
        GdkDisplay* display = gdk_display_get_default();
        gtk_style_context_remove_provider_for_display(
            display, GTK_STYLE_PROVIDER(cssProvider_));
        g_object_unref(cssProvider_);
        cssProvider_ = nullptr;
    }
    applyCss();
    showCategoryContent(selectedCategoryId_);
}

} // namespace termcore

#endif // __linux__
