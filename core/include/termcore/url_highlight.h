#ifndef TERMCORE_URL_HIGHLIGHT_H
#define TERMCORE_URL_HIGHLIGHT_H

#include "termcore/url_detector.h"
#include "termcore/config.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace termcore {

class Screen;

/// A URL span with hover state for rendering
struct UrlSpan {
    int row;
    int start_col;
    int end_col;      // exclusive
    std::string url;
    bool hovered = false;
};

/// Rendering hint consumed by the renderer (no URL string, just geometry + hover)
struct UrlRenderHint {
    int row;
    int start_col;
    int end_col;       // exclusive
    bool hovered;
};

/// Manages URL detection, hover tracking, and visual hints for clickable URLs.
/// Wraps UrlDetector and adds caching, hover state, and render hint generation.
class UrlHighlightManager {
public:
    UrlHighlightManager();
    ~UrlHighlightManager() = default;

    /// Scan visible screen rows for URLs using UrlDetector.
    /// Only rescans when the dirty flag is set (call markDirty() when screen changes).
    void scanScreen(const Screen& screen, int visible_rows);

    /// Update hover state given the current mouse position (grid coordinates).
    /// Returns true if hover state changed (caller should invalidate rendering).
    bool updateHover(int row, int col);

    /// Clear hover state (e.g., mouse left the terminal area).
    /// Returns true if hover state changed.
    bool clearHover();

    /// Returns the currently hovered URL span, if any.
    std::optional<UrlSpan> getHoveredUrl() const;

    /// Returns all detected URL spans in the visible area.
    const std::vector<UrlSpan>& getVisibleUrls() const { return urls_; }

    /// Returns render hints for all visible URLs (for renderer consumption).
    std::vector<UrlRenderHint> getRenderHints() const;

    /// Mark the cached URL data as stale (call when screen content changes).
    void markDirty() { dirty_ = true; }

    /// Whether clickable URLs feature is enabled.
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }

    /// URL color for non-hovered URLs (RGB, no alpha).
    uint32_t urlColor() const { return url_color_; }
    void setUrlColor(uint32_t color) { url_color_ = color; }

    /// Apply settings from Config.
    void applyConfig(const Config& config);

private:
    UrlDetector detector_;
    std::vector<UrlSpan> urls_;
    int hovered_index_ = -1;  // index into urls_, or -1
    bool dirty_ = true;
    bool enabled_ = true;
    uint32_t url_color_ = 0x89b4fa;
};

} // namespace termcore
#endif // TERMCORE_URL_HIGHLIGHT_H
