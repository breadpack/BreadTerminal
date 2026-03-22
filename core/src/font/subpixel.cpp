#include "termcore/font/subpixel.h"
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace termcore {

// ---------------------------------------------------------------------------
// String conversion: SubpixelMode
// ---------------------------------------------------------------------------

std::string subpixelModeToString(SubpixelMode mode) {
    switch (mode) {
        case SubpixelMode::None: return "none";
        case SubpixelMode::RGB:  return "rgb";
        case SubpixelMode::BGR:  return "bgr";
        case SubpixelMode::VRGB: return "vrgb";
        case SubpixelMode::VBGR: return "vbgr";
        case SubpixelMode::Auto: return "auto";
    }
    return "auto";
}

SubpixelMode subpixelModeFromString(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "none") return SubpixelMode::None;
    if (lower == "rgb")  return SubpixelMode::RGB;
    if (lower == "bgr")  return SubpixelMode::BGR;
    if (lower == "vrgb") return SubpixelMode::VRGB;
    if (lower == "vbgr") return SubpixelMode::VBGR;
    if (lower == "auto") return SubpixelMode::Auto;
    return SubpixelMode::Auto;  // default for unrecognized strings
}

// ---------------------------------------------------------------------------
// String conversion: HintingMode
// ---------------------------------------------------------------------------

std::string hintingModeToString(HintingMode mode) {
    switch (mode) {
        case HintingMode::None:   return "none";
        case HintingMode::Slight: return "slight";
        case HintingMode::Medium: return "medium";
        case HintingMode::Full:   return "full";
        case HintingMode::Auto:   return "auto";
    }
    return "auto";
}

HintingMode hintingModeFromString(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "none")   return HintingMode::None;
    if (lower == "slight") return HintingMode::Slight;
    if (lower == "medium") return HintingMode::Medium;
    if (lower == "full")   return HintingMode::Full;
    if (lower == "auto")   return HintingMode::Auto;
    return HintingMode::Auto;  // default for unrecognized strings
}

// ---------------------------------------------------------------------------
// System detection
// ---------------------------------------------------------------------------

#ifdef _WIN32

SubpixelMode detectSystemSubpixel() {
    // Read ClearType pixel structure from registry
    // HKCU\Software\Microsoft\Avalon.Graphics\DISPLAY1\PixelStructure
    // 0 = flat/unknown, 1 = RGB, 2 = BGR
    HKEY hkey = nullptr;
    LONG result = RegOpenKeyExA(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Avalon.Graphics\\DISPLAY1",
        0, KEY_READ, &hkey);

    if (result == ERROR_SUCCESS && hkey) {
        DWORD pixel_structure = 0;
        DWORD size = sizeof(pixel_structure);
        DWORD type = 0;
        result = RegQueryValueExA(hkey, "PixelStructure", nullptr,
                                  &type, reinterpret_cast<LPBYTE>(&pixel_structure), &size);
        RegCloseKey(hkey);

        if (result == ERROR_SUCCESS && type == REG_DWORD) {
            if (pixel_structure == 1) return SubpixelMode::RGB;
            if (pixel_structure == 2) return SubpixelMode::BGR;
        }
    }

    // Default: most LCD monitors use RGB
    return SubpixelMode::RGB;
}

HintingMode detectSystemHinting() {
    // Windows ClearType always uses full hinting equivalent
    return HintingMode::Full;
}

#elif defined(__APPLE__)

SubpixelMode detectSystemSubpixel() {
    // macOS handles subpixel AA internally; sub-pixel rendering was removed in Mojave
    return SubpixelMode::None;
}

HintingMode detectSystemHinting() {
    return HintingMode::Auto;
}

#else
// Linux / other Unix

SubpixelMode detectSystemSubpixel() {
    // Try reading Xft.rgba from X resources
    // Common values: "rgb", "bgr", "vrgb", "vbgr", "none"
    const char* xft_rgba = nullptr;

    // Check environment-based fontconfig setting
    // In practice, most Linux desktops use fontconfig; we read Xft.rgba
    // via the XRM database or fall back to fontconfig defaults.
    // For a non-X11 build we just parse the string if available.

    // Try XDG fontconfig: ~/.config/fontconfig/fonts.conf is complex XML,
    // so we rely on the Xft resource string which most DEs set.
    // A full implementation would use XGetDefault or fontconfig API;
    // here we check the XENVIRONMENT or parse xrdb output.

    xft_rgba = std::getenv("BREAD_SUBPIXEL");  // allow override
    if (xft_rgba) {
        return subpixelModeFromString(xft_rgba);
    }

    // Default: most Linux LCD panels are RGB
    return SubpixelMode::RGB;
}

HintingMode detectSystemHinting() {
    const char* hint = std::getenv("BREAD_HINTING");  // allow override
    if (hint) {
        return hintingModeFromString(hint);
    }

    // Default to slight hinting (common Linux default)
    return HintingMode::Slight;
}

#endif

} // namespace termcore
