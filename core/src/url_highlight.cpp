#include "termcore/url_highlight.h"
#include "termcore/screen.h"

namespace termcore {

UrlHighlightManager::UrlHighlightManager() = default;

void UrlHighlightManager::scanScreen(const Screen& screen, int visible_rows) {
    if (!enabled_) {
        if (!urls_.empty()) {
            urls_.clear();
            hovered_index_ = -1;
        }
        return;
    }

    if (!dirty_) return;
    dirty_ = false;

    // Save the previously hovered URL string so we can restore hover after rescan
    std::string prev_hovered_url;
    if (hovered_index_ >= 0 && hovered_index_ < static_cast<int>(urls_.size())) {
        prev_hovered_url = urls_[hovered_index_].url;
    }

    // Use UrlDetector to find all URLs in the visible screen area
    auto detected = detector_.detectInScreen(screen);

    urls_.clear();
    urls_.reserve(detected.size());
    for (auto& d : detected) {
        // Only include URLs within the visible row range
        if (d.row >= 0 && d.row < visible_rows) {
            urls_.push_back({d.row, d.start_col, d.end_col, std::move(d.url), false});
        }
    }

    // Try to restore hover state by matching URL string
    hovered_index_ = -1;
    if (!prev_hovered_url.empty()) {
        for (int i = 0; i < static_cast<int>(urls_.size()); ++i) {
            if (urls_[i].url == prev_hovered_url) {
                hovered_index_ = i;
                urls_[i].hovered = true;
                break;
            }
        }
    }
}

bool UrlHighlightManager::updateHover(int row, int col) {
    if (!enabled_) return false;

    int new_index = -1;
    for (int i = 0; i < static_cast<int>(urls_.size()); ++i) {
        const auto& u = urls_[i];
        if (u.row == row && col >= u.start_col && col < u.end_col) {
            new_index = i;
            break;
        }
    }

    if (new_index == hovered_index_) return false;

    // Clear old hover
    if (hovered_index_ >= 0 && hovered_index_ < static_cast<int>(urls_.size())) {
        urls_[hovered_index_].hovered = false;
    }

    // Set new hover
    hovered_index_ = new_index;
    if (hovered_index_ >= 0) {
        urls_[hovered_index_].hovered = true;
    }

    return true;
}

bool UrlHighlightManager::clearHover() {
    if (hovered_index_ < 0) return false;

    if (hovered_index_ < static_cast<int>(urls_.size())) {
        urls_[hovered_index_].hovered = false;
    }
    hovered_index_ = -1;
    return true;
}

std::optional<UrlSpan> UrlHighlightManager::getHoveredUrl() const {
    if (hovered_index_ >= 0 && hovered_index_ < static_cast<int>(urls_.size())) {
        return urls_[hovered_index_];
    }
    return std::nullopt;
}

std::vector<UrlRenderHint> UrlHighlightManager::getRenderHints() const {
    if (!enabled_) return {};

    std::vector<UrlRenderHint> hints;
    hints.reserve(urls_.size());
    for (const auto& u : urls_) {
        hints.push_back({u.row, u.start_col, u.end_col, u.hovered});
    }
    return hints;
}

void UrlHighlightManager::applyConfig(const Config& config) {
    enabled_ = config.clickable_urls;
    url_color_ = config.url_color;
    markDirty();  // force rescan with new settings
}

} // namespace termcore
