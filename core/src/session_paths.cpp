#include "termcore/session.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace termcore {

std::string SessionManager::defaultSessionDir() {
#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/Application Support/BreadTerminal/sessions";
#else
    // XDG_DATA_HOME or fallback
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/breadterminal/sessions";
    }
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.local/share/breadterminal/sessions";
#endif
}

std::string SessionManager::sessionFilePath(const std::string& dir) {
    std::string d = dir.empty() ? defaultSessionDir() : dir;
    if (d.empty()) return "";
    return d + "/last.json";
}

}  // namespace termcore
