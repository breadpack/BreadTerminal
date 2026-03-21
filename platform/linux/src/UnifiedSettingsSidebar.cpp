#if defined(__linux__)

#include "UnifiedSettingsWindow.h"

#include <algorithm>
#include <set>

namespace termcore {

// ---------------------------------------------------------------------------
// buildSidebar
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::buildSidebar() {
    sidebarScroll_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebarScroll_),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_add_css_class(sidebarScroll_, "sidebar");
    gtk_widget_set_size_request(sidebarScroll_, kUsSidebarMin, -1);

    sidebar_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebar_), GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(sidebar_, "sidebar");

    // Row selected callback
    g_signal_connect(sidebar_, "row-selected",
        G_CALLBACK(+[](GtkListBox* box, GtkListBoxRow* row, gpointer data) {
            auto* self = static_cast<UnifiedSettingsWindow*>(data);
            self->onSidebarRowSelected(box, row);
        }), this);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebarScroll_), sidebar_);
    gtk_paned_set_start_child(GTK_PANED(paned_), sidebarScroll_);

    rebuildSidebarRows();
}

// ---------------------------------------------------------------------------
// rebuildSidebarRows
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::rebuildSidebarRows() {
    if (!sidebar_) return;

    // Remove all existing rows
    GtkWidget* child = gtk_widget_get_first_child(sidebar_);
    while (child) {
        GtkWidget* next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(sidebar_), child);
        child = next;
    }

    if (!model_) return;

    // Build set of visible IDs for fast lookup
    std::set<std::string> visibleSet(visibleCategoryIds_.begin(),
                                     visibleCategoryIds_.end());

    int selectIdx = -1;
    int rowIdx = 0;

    auto topCats = model_->topLevelCategories();
    for (auto* top : topCats) {
        // Check if this top-level group has any visible subcategories
        auto subs = model_->subcategories(top->id);
        bool hasVisible = false;
        for (auto* sub : subs) {
            if (visibleSet.count(sub->id)) { hasVisible = true; break; }
        }
        if (!hasVisible) continue;

        // Section header (non-activatable)
        GtkWidget* headerLabel = gtk_label_new(top->label.c_str());
        gtk_label_set_xalign(GTK_LABEL(headerLabel), 0.0);
        gtk_widget_add_css_class(headerLabel, "category-header");

        GtkWidget* headerRow = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(headerRow), headerLabel);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(headerRow), FALSE);
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(headerRow), FALSE);
        gtk_list_box_append(GTK_LIST_BOX(sidebar_), headerRow);
        rowIdx++;

        // Subcategories
        for (auto* sub : subs) {
            if (!visibleSet.count(sub->id)) continue;

            GtkWidget* subLabel = gtk_label_new(sub->label.c_str());
            gtk_label_set_xalign(GTK_LABEL(subLabel), 0.0);

            GtkWidget* subRow = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(subRow), subLabel);

            // Store the category ID as widget name for lookup
            gtk_widget_set_name(subRow, sub->id.c_str());

            gtk_list_box_append(GTK_LIST_BOX(sidebar_), subRow);

            if (sub->id == selectedCategoryId_) {
                selectIdx = rowIdx;
            }
            rowIdx++;
        }
    }

    // Select the current category
    if (selectIdx >= 0) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(sidebar_), selectIdx);
        if (row) {
            gtk_list_box_select_row(GTK_LIST_BOX(sidebar_), row);
        }
    }
}

// ---------------------------------------------------------------------------
// onSidebarRowSelected
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onSidebarRowSelected(GtkListBox* /*box*/,
                                                   GtkListBoxRow* row) {
    if (!row) return;

    const char* name = gtk_widget_get_name(GTK_WIDGET(row));
    if (!name || name[0] == '\0') return;

    // Skip header rows (they don't have a category ID as name)
    std::string catId(name);

    // Verify it's a valid category
    if (model_ && model_->category(catId)) {
        selectedCategoryId_ = catId;
        showCategoryContent(selectedCategoryId_);
    }
}

} // namespace termcore

#endif // __linux__
