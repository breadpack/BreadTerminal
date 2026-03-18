#if !defined(_WIN32) && !defined(__APPLE__)

// webkit2gtk-based implementation stub
// Full implementation would use:
//   webkit_web_view_new()
//   webkit_web_view_load_uri()
//   webkit_web_view_go_back()
//   webkit_web_view_go_forward()
//   webkit_web_view_reload()
//   webkit_web_view_stop_loading()
//   webkit_web_view_get_uri()
//   webkit_web_view_get_title()
//   webkit_web_view_can_go_back()
//   webkit_web_view_can_go_forward()
//   webkit_web_view_is_loading()
//   webkit_web_view_run_javascript()

#include "termcore/webview.h"

namespace termcore {

std::unique_ptr<IWebView> createWebView() {
    // TODO: Implement WebKitGTK-based WebView
    return nullptr;
}

} // namespace termcore

#endif
