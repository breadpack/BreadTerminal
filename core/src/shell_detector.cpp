#include "termcore/profile.h"

namespace termcore {

#if defined(_WIN32)
std::vector<Profile> detectWindowsShells();
#else
std::vector<Profile> detectUnixShells();
#endif

std::vector<Profile> ShellDetector::detect() {
#if defined(_WIN32)
    return detectWindowsShells();
#else
    return detectUnixShells();
#endif
}

} // namespace termcore
