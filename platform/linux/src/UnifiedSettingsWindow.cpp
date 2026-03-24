#if defined(__linux__)

#include "UnifiedSettingsWindow.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <fontconfig/fontconfig.h>

namespace termcore {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

UnifiedSettingsWindow::UnifiedSettingsWindow() = default;

UnifiedSettingsWindow::~UnifiedSettingsWindow() {
    close();
}

// ---------------------------------------------------------------------------
// setConfig
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::setConfig(const Config& config) {
    config_ = config;
    defaultConfig_ = Config{};
    model_ = std::make_unique<SettingsModel>(config_, defaultConfig_);

    // Build allCategoryIds_ and visibleCategoryIds_
    allCategoryIds_.clear();
    visibleCategoryIds_.clear();
    auto topCats = model_->topLevelCategories();
    for (auto* top : topCats) {
        auto subs = model_->subcategories(top->id);
        for (auto* sub : subs) {
            allCategoryIds_.push_back(sub->id);
            visibleCategoryIds_.push_back(sub->id);
        }
    }

    // Load font_index.json
    {
        std::string path = findIndexPath("font_index.json");
        if (!path.empty()) {
            std::ifstream f(path);
            if (f.is_open()) {
                std::ostringstream ss;
                ss << f.rdbuf();
                fontIndexReady_ = fontIndex_.loadFromJSON(ss.str());
            }
        }
    }

    if (fontIndexReady_) {
        // Set up installed predicate using fontconfig
        fontIndex_.setInstalledPredicate([](const std::string& name) -> bool {
            if (name.empty()) return false;
            FcPattern* pat = FcPatternCreate();
            FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)name.c_str());
            FcConfigSubstitute(nullptr, pat, FcMatchPattern);
            FcDefaultSubstitute(pat);
            FcResult result;
            FcPattern* match = FcFontMatch(nullptr, pat, &result);
            bool found = false;
            if (match) {
                FcChar8* matchFamily = nullptr;
                if (FcPatternGetString(match, FC_FAMILY, 0, &matchFamily) == FcResultMatch) {
                    found = (strcasecmp((const char*)matchFamily, name.c_str()) == 0);
                }
                FcPatternDestroy(match);
            }
            FcPatternDestroy(pat);
            return found;
        });
        fontIndex_.refreshInstallStatus();

        // Enumerate system fonts and add any not already in the index
        FcPattern* pat = FcPatternCreate();
        FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, (char*)nullptr);
        FcFontSet* fs = FcFontList(nullptr, pat, os);
        if (fs) {
            for (int i = 0; i < fs->nfont; ++i) {
                FcChar8* family = nullptr;
                if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch) {
                    fontIndex_.addSystemFont(std::string((const char*)family));
                }
            }
            FcFontSetDestroy(fs);
        }
        FcObjectSetDestroy(os);
        FcPatternDestroy(pat);

        rebuildFontFilteredList();
    }

    // Load theme_index.json
    {
        std::string path = findIndexPath("theme_index.json");
        if (!path.empty()) {
            std::ifstream f(path);
            if (f.is_open()) {
                std::ostringstream ss;
                ss << f.rdbuf();
                themeIndex_.loadFromJSON(ss.str());
            }
        }
    }

    rebuildThemeFilteredList();
}

void UnifiedSettingsWindow::setSaveCallback(SaveCallback cb) {
    saveCallback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Show / Close
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::show(GtkWindow* parent) {
    if (window_) {
        gtk_window_present(window_);
        return;
    }
    parentWindow_ = parent;
    buildUI();
    gtk_window_present(window_);
}

void UnifiedSettingsWindow::close() {
    if (cssProvider_) {
        g_object_unref(cssProvider_);
        cssProvider_ = nullptr;
    }
    if (window_) {
        gtk_window_destroy(window_);
        window_ = nullptr;
    }
}

bool UnifiedSettingsWindow::isVisible() const {
    return window_ != nullptr;
}

// ---------------------------------------------------------------------------
// buildUI
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::buildUI() {
    // Create window
    window_ = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(window_, "Settings");
    gtk_window_set_default_size(window_, kUsWinWidth, kUsWinHeight);
    gtk_window_set_resizable(window_, TRUE);

    // Set minimum size using GtkConstraintLayout approach
    // GTK4 doesn't have gtk_widget_set_size_request for min, use constraint
    gtk_widget_set_size_request(GTK_WIDGET(window_), kUsMinWidth, kUsMinHeight);

    if (parentWindow_) {
        gtk_window_set_transient_for(window_, parentWindow_);
        gtk_window_set_modal(window_, FALSE);
    }

    // Apply CSS
    applyCss();

    // Handle window destroy
    g_signal_connect(window_, "destroy",
        G_CALLBACK(+[](GtkWindow*, gpointer data) {
            auto* self = static_cast<UnifiedSettingsWindow*>(data);
            self->window_ = nullptr;
        }), this);

    // Header bar with search entry and Open Lua button
    headerBar_ = gtk_header_bar_new();

    // Search entry (centered in header)
    searchEntry_ = gtk_search_entry_new();
    gtk_widget_set_size_request(searchEntry_, kUsSearchW, kUsSearchH);
    gtk_widget_set_hexpand(searchEntry_, FALSE);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(headerBar_), searchEntry_);

    g_signal_connect(searchEntry_, "search-changed",
        G_CALLBACK(+[](GtkSearchEntry* entry, gpointer data) {
            auto* self = static_cast<UnifiedSettingsWindow*>(data);
            const char* text = gtk_editable_get_text(GTK_EDITABLE(entry));
            self->onSearchChanged(text);
        }), this);

    // "Open Lua" button on the right
    GtkWidget* openLuaBtn = gtk_button_new_with_label("Open Lua");
    gtk_widget_add_css_class(openLuaBtn, "open-lua-btn");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(headerBar_), openLuaBtn);

    g_signal_connect(openLuaBtn, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer) {
            std::string configPath;
            const char* home = g_get_home_dir();
            if (home) {
                configPath = std::string(home) + "/.config/breadterminal/config.lua";
                // Create directory and file if needed
                std::string dir = std::string(home) + "/.config/breadterminal";
                g_mkdir_with_parents(dir.c_str(), 0755);

                if (!g_file_test(configPath.c_str(), G_FILE_TEST_EXISTS)) {
                    std::ofstream f(configPath);
                    if (f.is_open()) {
                        f << "-- BreadTerminal config\nreturn {}\n";
                    }
                }

                // Open with default editor using xdg-open
                std::string cmd = "xdg-open \"" + configPath + "\"";
                g_spawn_command_line_async(cmd.c_str(), nullptr);
            }
        }), nullptr);

    gtk_window_set_titlebar(window_, headerBar_);

    // Main vertical box: paned + status bar
    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(window_, mainBox);

    // Paned: sidebar | content
    paned_ = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned_), kUsSidebarDef);
    gtk_paned_set_shrink_start_child(GTK_PANED(paned_), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(paned_), FALSE);
    gtk_widget_set_vexpand(paned_, TRUE);
    gtk_box_append(GTK_BOX(mainBox), paned_);

    // Build sidebar
    buildSidebar();

    // Build content area
    buildContentArea();

    // Bottom status bar
    GtkWidget* statusBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(statusBar, "status-bar");
    gtk_widget_set_size_request(statusBar, -1, kUsBottomBarH);

    statusLabel_ = gtk_label_new("BreadTerminal");
    gtk_widget_set_halign(statusLabel_, GTK_ALIGN_START);
    gtk_widget_set_margin_start(statusLabel_, 8);
    gtk_widget_add_css_class(statusLabel_, "status-label");
    gtk_box_append(GTK_BOX(statusBar), statusLabel_);

    gtk_box_append(GTK_BOX(mainBox), statusBar);

    // Show initial category content
    showCategoryContent(selectedCategoryId_);
}

// ---------------------------------------------------------------------------
// applyCss
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::applyCss() {
    cssProvider_ = gtk_css_provider_new();

    // Derive colors from config
    uint32_t bg = config_.background;
    uint32_t fg = config_.foreground;

    auto hexColor = [](uint32_t c) -> std::string {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%06x", c & 0xFFFFFF);
        return buf;
    };

    // Compute dim/darker variants
    auto darken = [](uint32_t c, double factor) -> uint32_t {
        int r = (int)(((c >> 16) & 0xFF) * factor);
        int g = (int)(((c >> 8) & 0xFF) * factor);
        int b = (int)((c & 0xFF) * factor);
        return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    };

    auto lighten = [](uint32_t c, double factor) -> uint32_t {
        int r = std::min(255, (int)(((c >> 16) & 0xFF) * factor));
        int g = std::min(255, (int)(((c >> 8) & 0xFF) * factor));
        int b = std::min(255, (int)((c & 0xFF) * factor));
        return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    };

    std::string bgStr = hexColor(bg);
    std::string fgStr = hexColor(fg);
    std::string dimStr = hexColor(darken(fg, 0.5));
    std::string sidebarBg = hexColor(darken(bg, 0.85));
    std::string selectedBg = hexColor(lighten(bg, 1.3));
    std::string accentStr = "#007ACC";
    std::string statusBg = hexColor(darken(bg, 0.8));

    std::string css = R"CSS(
        window {
            background-color: )CSS" + bgStr + R"CSS(;
            color: )CSS" + fgStr + R"CSS(;
        }
        headerbar {
            background-color: )CSS" + sidebarBg + R"CSS(;
            color: )CSS" + fgStr + R"CSS(;
            border-bottom: 1px solid )CSS" + hexColor(darken(bg, 0.7)) + R"CSS(;
            min-height: )CSS" + std::to_string(kUsTopBarH) + R"CSS(px;
        }
        .sidebar {
            background-color: )CSS" + sidebarBg + R"CSS(;
        }
        .sidebar row {
            padding: 2px 8px 2px 28px;
            color: )CSS" + dimStr + R"CSS(;
        }
        .sidebar row:selected {
            background-color: rgba(0, 122, 204, 0.2);
            color: )CSS" + fgStr + R"CSS(;
        }
        .sidebar .category-header {
            font-weight: bold;
            color: )CSS" + dimStr + R"CSS(;
            padding: 4px 8px 2px 12px;
        }
        .content-area {
            background-color: )CSS" + bgStr + R"CSS(;
            padding: )CSS" + std::to_string(kUsContentPad) + R"CSS(px;
        }
        .section-title {
            font-size: 20px;
            font-weight: bold;
            margin-bottom: 16px;
            color: )CSS" + fgStr + R"CSS(;
        }
        .setting-label {
            font-weight: 600;
            font-size: 14px;
            color: )CSS" + fgStr + R"CSS(;
        }
        .setting-description {
            font-size: 12px;
            color: )CSS" + dimStr + R"CSS(;
        }
        .modified-indicator {
            background-color: #007ACC;
            min-width: 3px;
        }
        .status-bar {
            background-color: )CSS" + statusBg + R"CSS(;
            min-height: )CSS" + std::to_string(kUsBottomBarH) + R"CSS(px;
        }
        .status-label {
            font-size: 11px;
            color: )CSS" + dimStr + R"CSS(;
        }
        .filter-button {
            border-radius: 13px;
            padding: 2px 12px;
            min-height: 22px;
            font-size: 12px;
            background-color: )CSS" + hexColor(darken(bg, 0.7)) + R"CSS(;
            color: )CSS" + fgStr + R"CSS(;
            border: none;
        }
        .filter-button:checked,
        .filter-button.active {
            background-color: )CSS" + accentStr + R"CSS(;
            color: white;
        }
        .card-grid {
            padding: 8px 0;
        }
        .open-lua-btn {
            border-radius: 6px;
            padding: 2px 12px;
            font-size: 12px;
        }
    )CSS";

    gtk_css_provider_load_from_string(cssProvider_, css.c_str());

    GdkDisplay* display = gdk_display_get_default();
    gtk_style_context_add_provider_for_display(
        display,
        GTK_STYLE_PROVIDER(cssProvider_),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onSearchChanged(const char* text) {
    searchText_ = text ? text : "";
    rebuildVisibleCategories();
    rebuildSidebarRows();

    // If search is active and we have visible categories, show first match
    if (!searchText_.empty() && !visibleCategoryIds_.empty()) {
        selectedCategoryId_ = visibleCategoryIds_.front();
        showCategoryContent(selectedCategoryId_);
    } else if (searchText_.empty()) {
        showCategoryContent(selectedCategoryId_);
    }
}

void UnifiedSettingsWindow::rebuildVisibleCategories() {
    visibleCategoryIds_.clear();

    if (searchText_.empty()) {
        visibleCategoryIds_ = allCategoryIds_;
        searchMatches_.clear();
        return;
    }

    if (!model_) return;

    searchMatches_ = model_->search(searchText_);

    // Collect unique category IDs from search results
    std::vector<std::string> matchedIds;
    for (const auto& m : searchMatches_) {
        bool found = false;
        for (const auto& id : matchedIds) {
            if (id == m.categoryId) { found = true; break; }
        }
        if (!found) matchedIds.push_back(m.categoryId);
    }

    // Also include CardGrid/KeybindingList sections if their label matches
    for (const auto& catId : allCategoryIds_) {
        const auto* cat = model_->category(catId);
        if (!cat) continue;

        if (cat->sectionType != SectionType::Settings) {
            // Check if category label matches search
            std::string lowerLabel = cat->label;
            std::string lowerSearch = searchText_;
            for (auto& c : lowerLabel) c = tolower(c);
            for (auto& c : lowerSearch) c = tolower(c);
            if (lowerLabel.find(lowerSearch) != std::string::npos) {
                bool found = false;
                for (const auto& id : matchedIds) {
                    if (id == catId) { found = true; break; }
                }
                if (!found) matchedIds.push_back(catId);
            }
        }
    }

    visibleCategoryIds_ = matchedIds;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::notifySave() {
    if (model_) model_->refreshModified(config_);
    if (saveCallback_) saveCallback_(config_);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string UnifiedSettingsWindow::findIndexPath(const char* filename) const {
    // Check common locations
    std::string paths[] = {
        std::string("/usr/share/breadterminal/") + filename,
        std::string("/usr/local/share/breadterminal/") + filename,
    };

    // Also check relative to executable
    char exePath[4096] = {};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        std::string exeDir(exePath);
        auto pos = exeDir.rfind('/');
        if (pos != std::string::npos) {
            exeDir.resize(pos);
            // Check exe dir
            std::string p1 = exeDir + "/" + filename;
            if (g_file_test(p1.c_str(), G_FILE_TEST_EXISTS)) return p1;
            // Check exe/resources
            std::string p2 = exeDir + "/resources/" + filename;
            if (g_file_test(p2.c_str(), G_FILE_TEST_EXISTS)) return p2;
            // Check exe/../resources
            std::string p3 = exeDir + "/../resources/" + filename;
            if (g_file_test(p3.c_str(), G_FILE_TEST_EXISTS)) return p3;
        }
    }

    for (const auto& p : paths) {
        if (g_file_test(p.c_str(), G_FILE_TEST_EXISTS)) return p;
    }

    return {};
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::setCairoColor(cairo_t* cr, uint32_t rgb, double alpha) {
    double r = ((rgb >> 16) & 0xFF) / 255.0;
    double g = ((rgb >> 8) & 0xFF) / 255.0;
    double b = (rgb & 0xFF) / 255.0;
    cairo_set_source_rgba(cr, r, g, b, alpha);
}

void UnifiedSettingsWindow::setCairoColorDim(cairo_t* cr, uint32_t rgb, double alpha) {
    setCairoColor(cr, rgb, alpha);
}

} // namespace termcore

#endif // __linux__
