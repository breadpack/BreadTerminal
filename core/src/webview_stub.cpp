// Default stub for createWebView() — returns nullptr when no platform WebView is linked.
// Platform libraries (termcore_macos, termcore_linux) override this with real implementations.

#include "termcore/webview.h"

namespace termcore {

#if !defined(__APPLE__) && !defined(TERMCORE_HAS_WEBVIEW)
std::unique_ptr<IWebView> createWebView() {
    return nullptr;
}
#endif

} // namespace termcore
