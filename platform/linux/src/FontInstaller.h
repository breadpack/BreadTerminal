#pragma once
#if defined(__linux__)

#include <string>
#include <functional>

namespace termcore {

/// Download a font ZIP from URL, extract .ttf/.otf files, and install them
/// into ~/.local/share/fonts/. Calls progressCb with status messages.
/// Returns true if at least one font file was installed successfully.
bool installFontFromUrl(
    const std::string& url,
    const std::string& fontName,
    std::function<void(const std::string& status)> progressCb = nullptr);

/// Uninstall a font by removing its files from ~/.local/share/fonts/
/// and refreshing the fontconfig cache.
/// Returns true if at least one font file was removed.
bool uninstallFont(const std::string& fontName);

} // namespace termcore

#endif
