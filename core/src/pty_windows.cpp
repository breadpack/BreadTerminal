#if defined(_WIN32)

#include "termcore/pty.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace termcore {

class WindowsPty : public Pty {
public:
    WindowsPty() = default;
    ~WindowsPty() override { cleanup(); }

    bool spawn(const std::string& command,
               const std::vector<std::string>& args,
               const std::string& working_dir,
               int rows, int cols) override {
        // 1. Create pipes for PTY I/O
        HANDLE input_read = NULL, input_write = NULL;
        HANDLE output_read = NULL, output_write = NULL;

        if (!CreatePipe(&input_read, &input_write, NULL, 0) ||
            !CreatePipe(&output_read, &output_write, NULL, 0)) {
            return false;
        }

        // 2. Create pseudo console
        COORD size;
        size.X = static_cast<SHORT>(cols);
        size.Y = static_cast<SHORT>(rows);

        HRESULT hr = CreatePseudoConsole(size, input_read, output_write, 0, &hpc_);
        if (FAILED(hr)) {
            CloseHandle(input_read);
            CloseHandle(input_write);
            CloseHandle(output_read);
            CloseHandle(output_write);
            return false;
        }

        // Close the handles that the console now owns
        CloseHandle(input_read);
        CloseHandle(output_write);

        pipe_in_ = input_write;    // We write to this -> goes to PTY input
        pipe_out_ = output_read;   // We read from this <- comes from PTY output

        // 3. Set up startup info with pseudo console
        STARTUPINFOEXW si = {};
        si.StartupInfo.cb = sizeof(si);

        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
        si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(
            GetProcessHeap(), 0, attr_size);
        if (!si.lpAttributeList) {
            ClosePseudoConsole(hpc_);
            hpc_ = INVALID_HANDLE_VALUE;
            return false;
        }
        InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size);
        UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hpc_, sizeof(hpc_), NULL, NULL);

        // 4. Build command line
        std::wstring cmd_line;
        if (command.empty()) {
            // Default: cmd.exe
            wchar_t sys_dir[MAX_PATH];
            GetSystemDirectoryW(sys_dir, MAX_PATH);
            cmd_line = std::wstring(sys_dir) + L"\\cmd.exe";
        } else {
            // Convert command to wide string
            cmd_line = toWide(command);
            for (const auto& arg : args) {
                cmd_line += L" " + toWide(arg);
            }
        }

        // 5. Create process
        std::wstring wdir = working_dir.empty() ? L"" : toWide(working_dir);

        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessW(
            NULL,
            cmd_line.data(),
            NULL, NULL, FALSE,
            EXTENDED_STARTUPINFO_PRESENT,
            NULL,
            wdir.empty() ? NULL : wdir.c_str(),
            &si.StartupInfo,
            &pi
        );

        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

        if (!ok) {
            ClosePseudoConsole(hpc_);
            hpc_ = INVALID_HANDLE_VALUE;
            return false;
        }

        process_ = pi.hProcess;
        thread_ = pi.hThread;

        // Set pipe to non-blocking mode for reads
        DWORD mode = PIPE_NOWAIT;
        SetNamedPipeHandleState(pipe_out_, &mode, NULL, NULL);

        return true;
    }

    int read(char* buf, size_t buf_size) override {
        if (pipe_out_ == INVALID_HANDLE_VALUE) return -1;
        DWORD bytes_read = 0;
        BOOL ok = ReadFile(pipe_out_, buf, static_cast<DWORD>(buf_size), &bytes_read, NULL);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_NO_DATA) return 0;  // Non-blocking, nothing available
            return -1;
        }
        return static_cast<int>(bytes_read);
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
        ResizePseudoConsole(hpc_, size);
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

private:
    void cleanup() {
        if (hpc_ != INVALID_HANDLE_VALUE) {
            ClosePseudoConsole(hpc_);
            hpc_ = INVALID_HANDLE_VALUE;
        }
        if (pipe_in_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_in_);
            pipe_in_ = INVALID_HANDLE_VALUE;
        }
        if (pipe_out_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_out_);
            pipe_out_ = INVALID_HANDLE_VALUE;
        }
        if (thread_ != INVALID_HANDLE_VALUE) {
            CloseHandle(thread_);
            thread_ = INVALID_HANDLE_VALUE;
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

    HPCON hpc_ = INVALID_HANDLE_VALUE;
    HANDLE pipe_in_ = INVALID_HANDLE_VALUE;
    HANDLE pipe_out_ = INVALID_HANDLE_VALUE;
    HANDLE process_ = INVALID_HANDLE_VALUE;
    HANDLE thread_ = INVALID_HANDLE_VALUE;
};

std::unique_ptr<Pty> createPty() {
    return std::make_unique<WindowsPty>();
}

} // namespace termcore

#endif // _WIN32
