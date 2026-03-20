#pragma once
#if defined(_WIN32)

#include <string>
#include <vector>
#include <functional>

namespace termcore {

/// Download a font ZIP from URL, extract .ttf/.otf files, and install them
/// into the per-user font directory. Calls progressCb with status messages.
/// Returns true if at least one font file was installed successfully.
bool installFontFromUrl(
    const std::string& url,
    const std::string& fontName,
    std::function<void(const std::string& status)> progressCb = nullptr);

} // namespace termcore

#endif
