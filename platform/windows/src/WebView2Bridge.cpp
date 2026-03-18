#if defined(_WIN32)

// WebView2 (Chromium-based) implementation stub
// Full implementation would use:
//   CreateCoreWebView2EnvironmentWithOptions()
//   ICoreWebView2
//   ICoreWebView2Controller
//   ICoreWebView2NavigationCompletedEventHandler
//   Navigate(), GoBack(), GoForward(), Reload(), Stop()
//   get_Source(), get_DocumentTitle()
//   ExecuteScript()

#include "termcore/webview.h"

namespace termcore {

std::unique_ptr<IWebView> createWebView() {
    // TODO: Implement WebView2-based WebView
    return nullptr;
}

} // namespace termcore

#endif
