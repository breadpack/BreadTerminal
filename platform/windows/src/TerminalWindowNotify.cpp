#if defined(_WIN32)

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

namespace termcore {

static constexpr UINT kNotifyIconId = 1;
static constexpr UINT kNotifyCallbackMsg = WM_APP + 100;

static std::vector<wchar_t> utf8ToWide(const std::string& str) {
    if (str.empty()) return {L'\0'};

    int len = MultiByteToWideChar(CP_UTF8, 0,
                                  str.c_str(),
                                  static_cast<int>(str.size()),
                                  nullptr, 0);
    std::vector<wchar_t> wide(len + 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        str.c_str(),
                        static_cast<int>(str.size()),
                        wide.data(), len);
    return wide;
}

void initNotificationIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kNotifyIconId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = kNotifyCallbackMsg;
    nid.hIcon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    wcscpy_s(nid.szTip, L"BreadTerminal");

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Set version for modern balloon/toast behavior.
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void showWindowsNotification(HWND hwnd, const std::string& title,
                             const std::string& body) {
    auto wide_title = utf8ToWide(title);
    auto wide_body = utf8ToWide(body);

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kNotifyIconId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;

    wcsncpy_s(nid.szInfoTitle, wide_title.data(),
              _TRUNCATE);
    wcsncpy_s(nid.szInfo, wide_body.data(),
              _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void removeNotificationIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kNotifyIconId;

    Shell_NotifyIconW(NIM_DELETE, &nid);
}

} // namespace termcore

#endif // _WIN32
