#if defined(_WIN32)

#include "termcore/pty.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace termcore {

// --------------------------------------------------------------------
// Dynamic ConPTY loader — prefer Windows Terminal's conpty.dll
// --------------------------------------------------------------------
// Windows Terminal's ConPTY (OpenConsole.exe) passes all VT sequences
// through without filtering. System ConPTY (conhost.exe) strips mouse
// mode, alt screen, sync update, and other sequences. Loading WT's
// conpty.dll fixes all these issues.
// This approach is also used by Alacritty.

using PFN_CreatePseudoConsole  = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using PFN_ClosePseudoConsole   = void(WINAPI*)(HPCON);
using PFN_ResizePseudoConsole  = HRESULT(WINAPI*)(HPCON, COORD);

struct ConPtyApi {
    PFN_CreatePseudoConsole  create = nullptr;
    PFN_ClosePseudoConsole   close  = nullptr;
    PFN_ResizePseudoConsole  resize = nullptr;
    HMODULE hModule = nullptr;
    bool fromWT = false;
};

// Try to load conpty.dll from a specific path, resolving API function pointers.
static bool tryLoadConPty(ConPtyApi& api, const std::wstring& dllPath) {
    api.hModule = LoadLibraryExW(dllPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!api.hModule) return false;

    api.create = reinterpret_cast<PFN_CreatePseudoConsole>(
        GetProcAddress(api.hModule, "CreatePseudoConsole"));
    api.close = reinterpret_cast<PFN_ClosePseudoConsole>(
        GetProcAddress(api.hModule, "ClosePseudoConsole"));
    api.resize = reinterpret_cast<PFN_ResizePseudoConsole>(
        GetProcAddress(api.hModule, "ResizePseudoConsole"));

    if (api.create && api.close && api.resize) {
        api.fromWT = true;
        return true;
    }
    FreeLibrary(api.hModule);
    api = {};
    return false;
}

static ConPtyApi loadConPtyApi() {
    ConPtyApi api;

    // 1. Look for bundled conpty.dll next to BreadTerminal.exe.
    //    To enable full VT passthrough (mouse, alt screen, sync update),
    //    place OpenConsole.exe + conpty.dll (built from the MIT-licensed
    //    microsoft/terminal source) alongside the executable.
    {
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
            std::wstring dir(exePath);
            auto pos = dir.find_last_of(L"\\/");
            if (pos != std::wstring::npos) dir.resize(pos);
            std::wstring dll = dir + L"\\conpty.dll";
            if (GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES) {
                if (tryLoadConPty(api, dll)) return api;
            }
        }
    }

    // 2. Fall back to system kernel32.dll (limited VT passthrough)
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (k32) {
        api.create = reinterpret_cast<PFN_CreatePseudoConsole>(
            GetProcAddress(k32, "CreatePseudoConsole"));
        api.close = reinterpret_cast<PFN_ClosePseudoConsole>(
            GetProcAddress(k32, "ClosePseudoConsole"));
        api.resize = reinterpret_cast<PFN_ResizePseudoConsole>(
            GetProcAddress(k32, "ResizePseudoConsole"));
    }
    return api;
}

static const ConPtyApi& conPty() {
    static ConPtyApi api = []() {
        auto a = loadConPtyApi();
        OutputDebugStringW(a.fromWT
            ? L"BreadTerminal: Using bundled ConPTY (OpenConsole.exe)\n"
            : L"BreadTerminal: Using system ConPTY (conhost.exe)\n");
        return a;
    }();
    return api;
}

// --------------------------------------------------------------------
// Thread-safe ring buffer for PTY output
// --------------------------------------------------------------------
class RingBuffer {
public:
    static constexpr size_t kCapacity = 128 * 1024; // 128 KB

    size_t readAvailable() const {
        return write_pos_ - read_pos_;
    }

    // Write data into the ring buffer.  Returns number of bytes written.
    size_t write(const char* data, size_t len) {
        size_t avail = kCapacity - readAvailable();
        if (len > avail) len = avail;
        if (len == 0) return 0;

        size_t wpos = write_pos_ % kCapacity;
        size_t first = (std::min)(len, kCapacity - wpos);
        std::memcpy(buf_ + wpos, data, first);
        if (first < len) {
            std::memcpy(buf_, data + first, len - first);
        }
        write_pos_ += len;
        return len;
    }

    // Read data out of the ring buffer.  Returns number of bytes read.
    size_t read(char* dst, size_t len) {
        size_t avail = readAvailable();
        if (len > avail) len = avail;
        if (len == 0) return 0;

        size_t rpos = read_pos_ % kCapacity;
        size_t first = (std::min)(len, kCapacity - rpos);
        std::memcpy(dst, buf_ + rpos, first);
        if (first < len) {
            std::memcpy(dst + first, buf_, len - first);
        }
        read_pos_ += len;
        return len;
    }

private:
    char buf_[kCapacity]{};
    size_t write_pos_ = 0;
    size_t read_pos_ = 0;
};

// --------------------------------------------------------------------
// WindowsPty – Named Pipe + Overlapped I/O + dedicated reader thread
// --------------------------------------------------------------------
class WindowsPty : public Pty {
public:
    WindowsPty() = default;
    ~WindowsPty() override { cleanup(); }

    bool spawn(const std::string& command,
               const std::vector<std::string>& args,
               const std::string& working_dir,
               int rows, int cols,
               const std::vector<std::pair<std::string, std::string>>& env_vars = {}) override {

        // 1. Create named pipes for PTY I/O
        //    ConPTY gets the non-overlapped ends; we keep the overlapped read end.

        // --- Output pipe (PTY writes, we read) ---
        static std::atomic<int> pipe_counter{0};
        DWORD pid = GetCurrentProcessId();
        int cnt = pipe_counter.fetch_add(1);

        wchar_t out_name[128];
        wsprintfW(out_name, L"\\\\.\\pipe\\BreadTerminal-%lu-%d-out", pid, cnt);

        // Our read end: overlapped
        HANDLE out_read = CreateNamedPipeW(
            out_name,
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_WAIT,
            1,          // max instances
            0,          // out buffer size (irrelevant, inbound pipe)
            64 * 1024,  // in buffer size
            0,          // default timeout
            NULL);
        if (out_read == INVALID_HANDLE_VALUE) return false;

        // ConPTY's write end: non-overlapped
        HANDLE out_write = CreateFileW(
            out_name,
            GENERIC_WRITE,
            0, NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (out_write == INVALID_HANDLE_VALUE) {
            CloseHandle(out_read);
            return false;
        }

        // --- Input pipe (we write, PTY reads) ---
        //    Anonymous pipe is fine here – we only do synchronous writes.
        HANDLE input_read = NULL, input_write = NULL;
        if (!CreatePipe(&input_read, &input_write, NULL, 0)) {
            CloseHandle(out_read);
            CloseHandle(out_write);
            return false;
        }

        // 2. Create pseudo console
        COORD size;
        size.X = static_cast<SHORT>(cols);
        size.Y = static_cast<SHORT>(rows);

        HRESULT hr = conPty().create(size, input_read, out_write, 0, &hpc_);
        if (FAILED(hr)) {
            CloseHandle(input_read);
            CloseHandle(input_write);
            CloseHandle(out_read);
            CloseHandle(out_write);
            return false;
        }

        // Close the handles that the console now owns
        CloseHandle(input_read);
        CloseHandle(out_write);

        pipe_in_ = input_write;    // We write to this -> goes to PTY input
        pipe_out_ = out_read;      // Overlapped read end

        // 3. Create the data-available event (manual-reset)
        data_event_ = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!data_event_) {
            cleanup();
            return false;
        }

        // 4. Set up startup info with pseudo console
        STARTUPINFOEXW si = {};
        si.StartupInfo.cb = sizeof(si);

        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
        si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(
            GetProcessHeap(), 0, attr_size);
        if (!si.lpAttributeList) {
            cleanup();
            return false;
        }
        InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size);
        UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hpc_, sizeof(hpc_), NULL, NULL);

        // 5. Build command line
        std::wstring cmd_line;
        if (command.empty()) {
            wchar_t sys_dir[MAX_PATH];
            GetSystemDirectoryW(sys_dir, MAX_PATH);
            cmd_line = std::wstring(sys_dir) + L"\\cmd.exe";
        } else {
            cmd_line = toWide(command);
            for (const auto& arg : args) {
                cmd_line += L" " + toWide(arg);
            }
        }

        // 6. Build environment block with additional env vars
        //    - Override keys from env_vars replace inherited values
        //    - Filter out WT_SESSION / KITTY_WINDOW_ID to prevent child apps
        //      from detecting image protocol support (BreadTerminal doesn't
        //      render inline images yet, and ConPTY approximates them as
        //      colored cells which looks broken).
        std::wstring env_block;
        LPVOID env_ptr = NULL;
        DWORD create_flags = EXTENDED_STARTUPINFO_PRESENT;
        if (!env_vars.empty()) {
            // Collect override keys + keys to suppress from inherited env
            std::set<std::wstring> override_keys;
            for (const auto& [key, value] : env_vars) {
                std::wstring wk = toWide(key);
                for (auto& c : wk) c = towupper(c);
                override_keys.insert(wk);
            }
            // Also suppress env vars that signal image-capable terminals
            override_keys.insert(L"WT_SESSION");
            override_keys.insert(L"WT_PROFILE_ID");
            override_keys.insert(L"KITTY_WINDOW_ID");

            LPWCH current_env = GetEnvironmentStringsW();
            if (current_env) {
                LPWCH p = current_env;
                while (*p) {
                    std::wstring entry(p);
                    // Extract key (everything before first '=')
                    auto eq = entry.find(L'=');
                    if (eq != std::wstring::npos && eq > 0) {
                        std::wstring key = entry.substr(0, eq);
                        for (auto& c : key) c = towupper(c);
                        if (override_keys.count(key)) {
                            p += entry.size() + 1;
                            continue;  // Skip: will be overridden or suppressed
                        }
                    }
                    env_block += entry;
                    env_block += L'\0';
                    p += entry.size() + 1;
                }
                FreeEnvironmentStringsW(current_env);
            }
            for (const auto& [key, value] : env_vars) {
                env_block += toWide(key) + L"=" + toWide(value) + L'\0';
            }
            env_block += L'\0';
            env_ptr = env_block.data();
            create_flags |= CREATE_UNICODE_ENVIRONMENT;
        }

        // 7. Create process
        std::wstring wdir;
        if (working_dir.empty()) {
            const wchar_t* home = _wgetenv(L"USERPROFILE");
            if (home) wdir = home;
        } else {
            wdir = toWide(working_dir);
        }

        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessW(
            NULL,
            cmd_line.data(),
            NULL, NULL, FALSE,
            create_flags,
            env_ptr,
            wdir.empty() ? NULL : wdir.c_str(),
            &si.StartupInfo,
            &pi
        );

        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

        if (!ok) {
            cleanup();
            return false;
        }

        process_ = pi.hProcess;
        proc_thread_ = pi.hThread;

        // 8. Start the dedicated reader thread
        reader_running_.store(true, std::memory_order_release);
        reader_thread_ = std::thread(&WindowsPty::readerThreadFunc, this);

        return true;
    }

    int read(char* buf, size_t buf_size) override {
        std::lock_guard<std::mutex> lock(buf_mutex_);

        // Check for pipe closed + buffer empty
        if (ring_.readAvailable() == 0) {
            if (!reader_running_.load(std::memory_order_acquire)) {
                return -1; // EOF – reader has stopped and buffer is drained
            }
            return 0; // No data yet
        }

        size_t n = ring_.read(buf, buf_size);

        // If the buffer is now empty, reset the event so WaitForMultipleObjects
        // will properly block until new data arrives.
        if (ring_.readAvailable() == 0) {
            ResetEvent(data_event_);
        }

        return static_cast<int>(n);
    }

    int write(const char* data, size_t len) override {
        if (pipe_in_ == INVALID_HANDLE_VALUE) return -1;
        DWORD bytes_written = 0;
        BOOL ok = WriteFile(pipe_in_, data, static_cast<DWORD>(len), &bytes_written, NULL);
        return ok ? static_cast<int>(bytes_written) : -1;
    }

    void resize(int rows, int cols) override {
        if (hpc_ == INVALID_HANDLE_VALUE) return;
        COORD size;
        size.X = static_cast<SHORT>(cols);
        size.Y = static_cast<SHORT>(rows);
        conPty().resize(hpc_, size);
    }

    bool isAlive() const override {
        if (process_ == INVALID_HANDLE_VALUE) return false;
        DWORD exit_code;
        if (GetExitCodeProcess(process_, &exit_code)) {
            return exit_code == STILL_ACTIVE;
        }
        return false;
    }

    int pid() const override {
        return static_cast<int>(GetProcessId(process_));
    }

    int fd() const override {
        return -1;  // Not applicable on Windows
    }

    void* nativeReadHandle() const override {
        return data_event_;
    }

    int waitForExit() override {
        if (process_ == INVALID_HANDLE_VALUE) return -1;
        WaitForSingleObject(process_, INFINITE);
        DWORD exit_code;
        GetExitCodeProcess(process_, &exit_code);
        return static_cast<int>(exit_code);
    }

    void signal(int sig) override {
        if (process_ == INVALID_HANDLE_VALUE) return;
        if (sig == 15 || sig == 9) { // SIGTERM or SIGKILL
            TerminateProcess(process_, 1);
        }
    }

    // Refresh cached process name + shell foreground status (expensive syscalls).
    void refreshProcessCache() const {
        auto now = std::chrono::steady_clock::now();
        if (now - cached_process_time_ < std::chrono::milliseconds(500)) return;
        cached_process_time_ = now;

        if (process_ == INVALID_HANDLE_VALUE) {
            cached_process_name_.clear();
            cached_is_shell_fg_ = true;
            return;
        }

        DWORD shellPid = GetProcessId(process_);
        if (shellPid == 0) {
            cached_process_name_.clear();
            cached_is_shell_fg_ = true;
            return;
        }

        // Walk process tree to find deepest child
        DWORD targetPid = shellPid;
        bool hasChild = false;
        for (int depth = 0; depth < 10; ++depth) {
            DWORD childPid = findChildProcess(targetPid);
            if (childPid == 0) break;
            targetPid = childPid;
            hasChild = true;
        }
        cached_is_shell_fg_ = !hasChild;

        // Get process name
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, targetPid);
        if (!proc) { cached_process_name_.clear(); return; }
        wchar_t path[MAX_PATH];
        DWORD pathLen = MAX_PATH;
        std::string name;
        if (QueryFullProcessImageNameW(proc, 0, path, &pathLen)) {
            std::wstring ws(path, pathLen);
            auto slash = ws.find_last_of(L"\\/");
            std::wstring base = (slash != std::wstring::npos) ? ws.substr(slash + 1) : ws;
            int sz = WideCharToMultiByte(CP_UTF8, 0, base.c_str(), -1, NULL, 0, NULL, NULL);
            if (sz > 0) {
                name.resize(sz - 1);
                WideCharToMultiByte(CP_UTF8, 0, base.c_str(), -1, name.data(), sz, NULL, NULL);
            }
        }
        CloseHandle(proc);

        if (name.size() > 4) {
            std::string suffix = name.substr(name.size() - 4);
            if (suffix == ".exe" || suffix == ".EXE") {
                name = name.substr(0, name.size() - 4);
            }
        }
        cached_process_name_ = std::move(name);
    }

    std::string foregroundProcessName() const override {
        refreshProcessCache();
        return cached_process_name_;
    }

    bool isShellForeground() const override {
        refreshProcessCache();
        return cached_is_shell_fg_;
    }

    bool isFullVtPassthrough() const override {
        return conPty().fromWT;
    }

    std::string foregroundCwd() const override {
        if (process_ == INVALID_HANDLE_VALUE) return {};
        DWORD shellPid = GetProcessId(process_);
        if (shellPid == 0) return {};

        DWORD targetPid = shellPid;
        for (int depth = 0; depth < 10; ++depth) {
            DWORD childPid = findChildProcess(targetPid);
            if (childPid == 0) break;
            targetPid = childPid;
        }

        return getProcessCwd(targetPid);
    }

private:
    // ------------------------------------------------------------------
    // Dedicated reader thread: reads from the overlapped named pipe
    // and fills the ring buffer.
    // ------------------------------------------------------------------
    void readerThreadFunc() {
        constexpr DWORD kReadBufSize = 64 * 1024; // 64 KB per read
        char tmp[kReadBufSize];

        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ov.hEvent) {
            reader_running_.store(false, std::memory_order_release);
            SetEvent(data_event_); // Wake any waiter so they see EOF
            return;
        }

        while (true) {
            DWORD bytes_read = 0;
            ResetEvent(ov.hEvent);

            BOOL ok = ReadFile(pipe_out_, tmp, kReadBufSize, &bytes_read, &ov);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    // Wait for data or cancellation.  We also watch for
                    // reader_running_ being cleared, but the simplest approach
                    // is to CancelIoEx from the main thread, which will
                    // complete this wait.
                    DWORD wait = WaitForSingleObject(ov.hEvent, INFINITE);
                    if (wait != WAIT_OBJECT_0) break;

                    if (!GetOverlappedResult(pipe_out_, &ov, &bytes_read, FALSE)) {
                        break; // Pipe closed or error
                    }
                } else {
                    // ERROR_BROKEN_PIPE, ERROR_OPERATION_ABORTED, etc.
                    break;
                }
            }

            if (bytes_read == 0) {
                // Pipe closed gracefully
                break;
            }

            // Copy into ring buffer
            {
                std::lock_guard<std::mutex> lock(buf_mutex_);
                size_t offset = 0;
                while (offset < bytes_read) {
                    size_t written = ring_.write(tmp + offset, bytes_read - offset);
                    offset += written;
                    if (written == 0) {
                        // Buffer full – overwrite oldest data by advancing read position.
                        // In practice this should be rare with a 128 KB buffer.
                        // We'll just drop the overflow.
                        break;
                    }
                }
            }
            // Signal that data is available
            SetEvent(data_event_);
        }

        CloseHandle(ov.hEvent);
        reader_running_.store(false, std::memory_order_release);
        // Signal so the main thread's WaitForMultipleObjects sees the change
        // (it will then call read() and get -1 once the buffer is drained).
        SetEvent(data_event_);
    }

    // ------------------------------------------------------------------
    // NtQueryInformationProcess (for foregroundCwd)
    // ------------------------------------------------------------------
    using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(
        HANDLE ProcessHandle,
        PROCESSINFOCLASS ProcessInformationClass,
        PVOID ProcessInformation,
        ULONG ProcessInformationLength,
        PULONG ReturnLength);

    static NtQueryInformationProcessFn getNtQueryInformationProcess() {
        static NtQueryInformationProcessFn fn = [] {
            HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (!ntdll) return static_cast<NtQueryInformationProcessFn>(nullptr);
            return reinterpret_cast<NtQueryInformationProcessFn>(
                GetProcAddress(ntdll, "NtQueryInformationProcess"));
        }();
        return fn;
    }

    static DWORD findChildProcess(DWORD parentPid) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        DWORD childPid = 0;
        FILETIME latestCreation = {};

        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ParentProcessID == parentPid) {
                    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                    if (proc) {
                        FILETIME creation, exit, kernel, user;
                        if (GetProcessTimes(proc, &creation, &exit, &kernel, &user)) {
                            if (CompareFileTime(&creation, &latestCreation) > 0) {
                                latestCreation = creation;
                                childPid = pe.th32ProcessID;
                            }
                        }
                        CloseHandle(proc);
                    }
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return childPid;
    }

    static std::string getProcessCwd(DWORD pid) {
        auto ntQuery = getNtQueryInformationProcess();
        if (!ntQuery) return {};

        HANDLE proc = OpenProcess(
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!proc) return {};

        std::string result;

        PROCESS_BASIC_INFORMATION pbi = {};
        ULONG retLen = 0;
        NTSTATUS status = ntQuery(
            proc, ProcessBasicInformation, &pbi, sizeof(pbi), &retLen);
        if (status != 0 || !pbi.PebBaseAddress) {
            CloseHandle(proc);
            return {};
        }

        PVOID paramsPtr = nullptr;
        SIZE_T bytesRead = 0;

#ifdef _WIN64
        constexpr SIZE_T kPebParamsOffset = 0x20;
#else
        constexpr SIZE_T kPebParamsOffset = 0x10;
#endif
        auto pebAddr = reinterpret_cast<BYTE*>(pbi.PebBaseAddress);
        if (!ReadProcessMemory(proc, pebAddr + kPebParamsOffset,
                               &paramsPtr, sizeof(paramsPtr), &bytesRead)
            || !paramsPtr) {
            CloseHandle(proc);
            return {};
        }

#ifdef _WIN64
        constexpr SIZE_T kParamsCurrentDirOffset = 0x38;
#else
        constexpr SIZE_T kParamsCurrentDirOffset = 0x24;
#endif
        UNICODE_STRING ustr = {};
        auto paramsAddr = reinterpret_cast<BYTE*>(paramsPtr);
        if (!ReadProcessMemory(proc, paramsAddr + kParamsCurrentDirOffset,
                               &ustr, sizeof(ustr), &bytesRead)) {
            CloseHandle(proc);
            return {};
        }

        if (ustr.Length == 0 || !ustr.Buffer) {
            CloseHandle(proc);
            return {};
        }

        std::wstring wpath(ustr.Length / sizeof(wchar_t), L'\0');
        if (!ReadProcessMemory(proc, ustr.Buffer,
                               wpath.data(), ustr.Length, &bytesRead)) {
            CloseHandle(proc);
            return {};
        }
        CloseHandle(proc);

        if (wpath.size() > 3 && wpath.back() == L'\\') {
            wpath.pop_back();
        }

        int sz = WideCharToMultiByte(
            CP_UTF8, 0, wpath.c_str(), static_cast<int>(wpath.size()),
            NULL, 0, NULL, NULL);
        if (sz > 0) {
            result.resize(sz);
            WideCharToMultiByte(
                CP_UTF8, 0, wpath.c_str(), static_cast<int>(wpath.size()),
                result.data(), sz, NULL, NULL);
        }

        return result;
    }

    void cleanup() {
        // 1. Close the pseudo console first – this causes ConPTY to close its
        //    pipe end, which will unblock the reader thread's ReadFile/overlapped wait.
        if (hpc_ != INVALID_HANDLE_VALUE) {
            conPty().close(hpc_);
            hpc_ = INVALID_HANDLE_VALUE;
        }

        // 2. Cancel any pending I/O on the read pipe so the reader thread wakes up.
        if (pipe_out_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe_out_, NULL);
        }

        // 3. Join the reader thread
        if (reader_thread_.joinable()) {
            reader_thread_.join();
        }

        // 4. Close handles
        if (pipe_in_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_in_);
            pipe_in_ = INVALID_HANDLE_VALUE;
        }
        if (pipe_out_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_out_);
            pipe_out_ = INVALID_HANDLE_VALUE;
        }
        if (data_event_) {
            CloseHandle(data_event_);
            data_event_ = NULL;
        }
        if (proc_thread_ != INVALID_HANDLE_VALUE) {
            CloseHandle(proc_thread_);
            proc_thread_ = INVALID_HANDLE_VALUE;
        }
        if (process_ != INVALID_HANDLE_VALUE) {
            CloseHandle(process_);
            process_ = INVALID_HANDLE_VALUE;
        }
    }

    static std::wstring toWide(const std::string& s) {
        if (s.empty()) return L"";
        int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
        std::wstring result(sz - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), sz);
        return result;
    }

    // ConPTY handle
    HPCON hpc_ = INVALID_HANDLE_VALUE;

    // Pipe handles
    HANDLE pipe_in_ = INVALID_HANDLE_VALUE;   // Write end (input to PTY)
    HANDLE pipe_out_ = INVALID_HANDLE_VALUE;  // Read end (overlapped named pipe)

    // Process handles
    HANDLE process_ = INVALID_HANDLE_VALUE;
    HANDLE proc_thread_ = INVALID_HANDLE_VALUE;

    // Data-available event – returned by nativeReadHandle() for
    // WaitForMultipleObjects integration.  Manual-reset: set when data
    // is in the buffer, reset when the buffer is drained.
    HANDLE data_event_ = NULL;

    // Reader thread + shared buffer
    std::thread reader_thread_;
    std::atomic<bool> reader_running_{false};
    std::mutex buf_mutex_;
    RingBuffer ring_;

    // Cached process info (expensive syscalls — refresh at most every 500ms)
    mutable std::string cached_process_name_;
    mutable bool cached_is_shell_fg_ = true;
    mutable std::chrono::steady_clock::time_point cached_process_time_{};
};

std::unique_ptr<Pty> createPty() {
    return std::make_unique<WindowsPty>();
}

} // namespace termcore

#endif // _WIN32
