#if defined(__linux__)

#include "UnifiedSettingsWindow.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace termcore {

// ---------------------------------------------------------------------------
// Font card draw data (passed to Cairo draw function)
// ---------------------------------------------------------------------------

struct FontCardDrawData {
    UnifiedSettingsWindow* window;
    const FontMetadata* meta;
    bool isActive;
    bool isInstalled;
    std::string fontFamily; // config font_family for active check
};

// ---------------------------------------------------------------------------
// rebuildFontFilteredList
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::rebuildFontFilteredList() {
    filteredFonts_.clear();

    bool installed = (activeFontFilter_ == FontFilter::Installed);
    bool nerd      = (activeFontFilter_ == FontFilter::NerdFonts);
    bool liga      = (activeFontFilter_ == FontFilter::Ligatures);

    if (installed || nerd || liga) {
        filteredFonts_ = fontIndex_.filter(installed, nerd, liga);
    } else {
        for (auto& f : fontIndex_.all())
            filteredFonts_.push_back(&f);
    }
}

// ---------------------------------------------------------------------------
// showFontCards
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::showFontCards() {
    if (filteredFonts_.empty() && fontIndex_.count() > 0) {
        rebuildFontFilteredList();
    }

    // --- Filter bar ---
    GtkWidget* filterBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, kFcFilterGap);
    gtk_widget_set_margin_bottom(filterBox, 12);

    static const char* filterLabels[] = {"All", "Installed", "NerdFonts", "Ligatures"};

    for (int i = 0; i < 4; ++i) {
        GtkWidget* btn = gtk_toggle_button_new_with_label(filterLabels[i]);
        gtk_widget_add_css_class(btn, "filter-button");
        if (i == static_cast<int>(activeFontFilter_)) {
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
                    self->onFontFilterChanged(idx);
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
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flowBox), kFcCardGap);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flowBox), kFcCardGap);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flowBox), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(flowBox, "card-grid");

    auto iequal = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t j = 0; j < a.size(); ++j)
            if (tolower((unsigned char)a[j]) != tolower((unsigned char)b[j]))
                return false;
        return true;
    };

    for (size_t i = 0; i < filteredFonts_.size(); ++i) {
        const FontMetadata* meta = filteredFonts_[i];

        bool isActive = false;
        if (!config_.font_family.empty()) {
            isActive = iequal(meta->name, config_.font_family)
                    || iequal(meta->postscript_name, config_.font_family);
        }

        // Card widget: GtkDrawingArea for Cairo custom rendering
        GtkWidget* cardArea = gtk_drawing_area_new();
        gtk_widget_set_size_request(cardArea, kFcCardW, 100); // preview area

        auto* drawData = new FontCardDrawData{
            this, meta, isActive, meta->installed, config_.font_family};

        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(cardArea),
            UnifiedSettingsWindow::drawFontCard, drawData, nullptr);

        g_signal_connect(cardArea, "destroy",
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                delete static_cast<FontCardDrawData*>(data);
            }), drawData);

        GtkWidget* cardBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(cardBox), cardArea);

        // Badge row
        GtkWidget* badgeBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_margin_top(badgeBox, 4);
        gtk_widget_set_margin_start(badgeBox, 4);

        if (meta->installed) {
            GtkWidget* badge = gtk_label_new("Installed");
            gtk_widget_add_css_class(badge, "filter-button");
            gtk_widget_add_css_class(badge, "active");
            gtk_box_append(GTK_BOX(badgeBox), badge);
        }
        if (meta->has_ligatures) {
            GtkWidget* badge = gtk_label_new("Ligatures");
            gtk_widget_add_css_class(badge, "filter-button");
            gtk_box_append(GTK_BOX(badgeBox), badge);
        }
        if (meta->has_nerd_font_variant) {
            GtkWidget* badge = gtk_label_new("Nerd Font");
            gtk_widget_add_css_class(badge, "filter-button");
            gtk_box_append(GTK_BOX(badgeBox), badge);
        }

        gtk_box_append(GTK_BOX(cardBox), badgeBox);

        // Bottom row: name + action button
        GtkWidget* nameLabel = gtk_label_new(meta->name.c_str());
        gtk_label_set_xalign(GTK_LABEL(nameLabel), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(nameLabel), PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_top(nameLabel, 4);

        GtkWidget* actionBtn;
        if (isActive) {
            actionBtn = gtk_button_new_with_label("Applied");
            gtk_widget_set_sensitive(actionBtn, FALSE);
        } else if (meta->installed) {
            actionBtn = gtk_button_new_with_label("Apply");
        } else {
            actionBtn = gtk_button_new_with_label("Install");
        }
        gtk_widget_set_halign(actionBtn, GTK_ALIGN_END);

        int* cardIdx = new int(static_cast<int>(i));
        g_object_set_data(G_OBJECT(actionBtn), "settings-window", this);
        g_object_set_data(G_OBJECT(actionBtn), "card-index", cardIdx);

        g_signal_connect(actionBtn, "clicked",
            G_CALLBACK(+[](GtkButton* btn, gpointer) {
                auto* self = static_cast<UnifiedSettingsWindow*>(
                    g_object_get_data(G_OBJECT(btn), "settings-window"));
                auto* idxPtr = static_cast<int*>(
                    g_object_get_data(G_OBJECT(btn), "card-index"));
                if (self && idxPtr) {
                    self->onFontCardClick(*idxPtr);
                }
            }), nullptr);

        g_signal_connect(actionBtn, "destroy",
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                delete static_cast<int*>(data);
            }), cardIdx);

        GtkWidget* bottomRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_margin_top(bottomRow, 4);
        gtk_widget_set_hexpand(nameLabel, TRUE);
        gtk_box_append(GTK_BOX(bottomRow), nameLabel);
        gtk_box_append(GTK_BOX(bottomRow), actionBtn);

        gtk_box_append(GTK_BOX(cardBox), bottomRow);

        gtk_flow_box_append(GTK_FLOW_BOX(flowBox), cardBox);
    }

    gtk_box_append(GTK_BOX(contentBox_), flowBox);

    // "No fonts found" message
    if (filteredFonts_.empty()) {
        GtkWidget* emptyLabel = gtk_label_new("No fonts found");
        gtk_widget_add_css_class(emptyLabel, "setting-description");
        gtk_box_append(GTK_BOX(contentBox_), emptyLabel);
    }
}

// ---------------------------------------------------------------------------
// drawFontCard — Cairo draw function for a single font card
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::drawFontCard(GtkDrawingArea* /*area*/,
                                           cairo_t* cr,
                                           int width, int height,
                                           gpointer data) {
    auto* d = static_cast<FontCardDrawData*>(data);
    if (!d || !d->meta) return;

    double w = width;
    double h = height;
    double r = 8.0;

    // Rounded rect helper
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
    if (d->isInstalled) {
        cairo_set_source_rgba(cr, 0.15, 0.15, 0.2, 1.0);
    } else {
        cairo_set_source_rgba(cr, 0.10, 0.10, 0.15, 1.0);
    }
    cairo_fill(cr);

    // Green left bar for installed fonts
    if (d->isInstalled) {
        cairo_set_source_rgba(cr, 0.2, 0.7, 0.3, 1.0);
        cairo_rectangle(cr, 0, r, 3, h - 2 * r);
        cairo_fill(cr);
    }

    // Font preview area
    if (d->isInstalled) {
        // Use the font itself for preview (or fallback to monospace)
        cairo_select_font_face(cr, d->meta->name.c_str(),
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 16.0);
        cairo_set_source_rgba(cr, 0.86, 0.86, 0.94, 1.0);

        cairo_move_to(cr, 12, 28);
        cairo_show_text(cr, "AaBb 0123");

        cairo_move_to(cr, 12, 52);
        cairo_show_text(cr, "!= => ->");

        // Check if font was actually used (fallback detection is approximate)
        cairo_text_extents_t extents;
        cairo_text_extents(cr, "A", &extents);
        if (extents.width < 0.1) {
            // Fallback to monospace
            cairo_select_font_face(cr, "monospace",
                                   CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 16.0);
            cairo_set_source_rgba(cr, 0.86, 0.86, 0.94, 0.5);
            cairo_move_to(cr, 12, 28);
            cairo_show_text(cr, "AaBb 0123");
            cairo_move_to(cr, 12, 52);
            cairo_show_text(cr, "!= => ->");

            // "(fallback)" label
            cairo_select_font_face(cr, "sans-serif",
                                   CAIRO_FONT_SLANT_ITALIC,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(cr, 9.0);
            cairo_set_source_rgba(cr, 0.5, 0.5, 0.6, 0.7);
            cairo_move_to(cr, w - 60, h - 6);
            cairo_show_text(cr, "(fallback)");
        }
    } else {
        // Not installed: show font name centered
        cairo_select_font_face(cr, "sans-serif",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 15.0);
        cairo_set_source_rgba(cr, 0.63, 0.63, 0.7, 1.0);

        cairo_text_extents_t extents;
        cairo_text_extents(cr, d->meta->name.c_str(), &extents);
        double tx = (w - extents.width) / 2.0;
        double ty = h / 2.0 - 4;
        cairo_move_to(cr, tx, ty);
        cairo_show_text(cr, d->meta->name.c_str());

        // "Not Installed" sub-label
        cairo_set_font_size(cr, 10.0);
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.6, 0.6);
        cairo_text_extents(cr, "Not Installed", &extents);
        tx = (w - extents.width) / 2.0;
        cairo_move_to(cr, tx, ty + 18);
        cairo_show_text(cr, "Not Installed");
    }
}

// ---------------------------------------------------------------------------
// onFontFilterChanged
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onFontFilterChanged(int filterIndex) {
    activeFontFilter_ = static_cast<FontFilter>(filterIndex);
    rebuildFontFilteredList();
    showCategoryContent(selectedCategoryId_);
}

// ---------------------------------------------------------------------------
// onFontCardClick
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onFontCardClick(int idx) {
    if (idx < 0 || idx >= (int)filteredFonts_.size()) return;
    const FontMetadata* meta = filteredFonts_[idx];
    if (!meta) return;

    if (meta->installed) {
        // Apply font
        config_.font_family = meta->name;
        notifySave();
        showCategoryContent(selectedCategoryId_);
    } else {
        // TODO: Install font (requires FontInstaller integration for Linux)
        g_debug("BreadTerminal: font install not yet implemented on Linux: %s",
                meta->name.c_str());
    }
}

} // namespace termcore

#endif // __linux__
