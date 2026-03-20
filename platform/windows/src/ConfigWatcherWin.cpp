#if defined(_WIN32)

#include "ConfigWatcherWin.h"
#include "termcore/config.h"
#include "termcore/config_diff.h"
#include "termcore/lua_config.h"

#include <algorithm>
#include <vector>

namespace termcore {

ConfigWatcherWin::~ConfigWatcherWin() {
    stop();
}

void ConfigWatcherWin::start(const std::string& path,
                             ConfigReloadCallback callback) {
    stop();

    config_path_ = path;
    callback_ = std::move(callback);

    // Extract directory and filename from the path.
    std::string normalized = config_path_;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');

    auto sep = normalized.rfind('\\');
    if (sep != std::string::npos) {
        watch_dir_ = normalized.substr(0, sep);
        watch_filename_ = normalized.substr(sep + 1);
    } else {
        watch_dir_ = ".";
        watch_filename_ = normalized;
    }

    // Load initial config so we can diff later (Lua first, then legacy).
    last_config_ = loadConfig();

    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event_ == nullptr) {
        stop_event_ = INVALID_HANDLE_VALUE;
        return;
    }

    running_ = true;
    watch_thread_ = std::thread(&ConfigWatcherWin::watchThread, this);
}

void ConfigWatcherWin::stop() {
    if (!running_) return;

    running_ = false;

    if (stop_event_ != INVALID_HANDLE_VALUE) {
        SetEvent(stop_event_);
    }

    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }

    if (stop_event_ != INVALID_HANDLE_VALUE) {
        CloseHandle(stop_event_);
        stop_event_ = INVALID_HANDLE_VALUE;
    }

    callback_ = nullptr;
}

void ConfigWatcherWin::reloadNow() {
    if (!callback_) return;

    std::string error;
    Config new_config;
    try {
        new_config = loadConfig();
    } catch (const std::exception& e) {
        error = e.what();
    }

    ConfigDirtyFlags dirty = diffConfig(last_config_, new_config);
    last_config_ = new_config;

    callback_(new_config, dirty, error);
}

void ConfigWatcherWin::watchThread() {
    // Convert watch_dir_ to wide string for CreateFileW.
    int wide_len = MultiByteToWideChar(CP_UTF8, 0,
                                       watch_dir_.c_str(),
                                       static_cast<int>(watch_dir_.size()),
                                       nullptr, 0);
    if (wide_len <= 0) return;

    std::vector<wchar_t> wide_dir(wide_len + 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        watch_dir_.c_str(),
                        static_cast<int>(watch_dir_.size()),
                        wide_dir.data(), wide_len);

    HANDLE dir_handle = CreateFileW(
        wide_dir.data(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (dir_handle == INVALID_HANDLE_VALUE) return;

    alignas(DWORD) char buffer[4096];
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
        CloseHandle(dir_handle);
        return;
    }

    while (running_) {
        ResetEvent(overlapped.hEvent);

        BOOL ok = ReadDirectoryChangesW(
            dir_handle,
            buffer,
            sizeof(buffer),
            FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            nullptr,
            &overlapped,
            nullptr);

        if (!ok) break;

        HANDLE handles[2] = {overlapped.hEvent, stop_event_};
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (wait == WAIT_OBJECT_0 + 1) {
            // Stop requested.
            CancelIo(dir_handle);
            break;
        }

        if (wait != WAIT_OBJECT_0) break;

        DWORD bytes_transferred = 0;
        if (!GetOverlappedResult(dir_handle, &overlapped,
                                 &bytes_transferred, FALSE)) {
            break;
        }

        // Walk the notification buffer to check if our file changed.
        bool our_file_changed = false;
        auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
        for (;;) {
            std::wstring changed_name(info->FileName,
                                      info->FileNameLength / sizeof(wchar_t));

            // Convert watch_filename_ to wide for comparison.
            int fn_wide_len = MultiByteToWideChar(
                CP_UTF8, 0,
                watch_filename_.c_str(),
                static_cast<int>(watch_filename_.size()),
                nullptr, 0);
            std::vector<wchar_t> fn_wide(fn_wide_len + 1, L'\0');
            MultiByteToWideChar(
                CP_UTF8, 0,
                watch_filename_.c_str(),
                static_cast<int>(watch_filename_.size()),
                fn_wide.data(), fn_wide_len);

            if (_wcsicmp(changed_name.c_str(), fn_wide.data()) == 0 ||
                _wcsicmp(changed_name.c_str(), L"config.lua") == 0) {
                our_file_changed = true;
                break;
            }

            if (info->NextEntryOffset == 0) break;
            info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<char*>(info) + info->NextEntryOffset);
        }

        if (our_file_changed) {
            // Small delay to let the writing process finish.
            Sleep(100);
            reloadNow();
        }
    }

    CloseHandle(overlapped.hEvent);
    CloseHandle(dir_handle);
}

} // namespace termcore

#endif // _WIN32
