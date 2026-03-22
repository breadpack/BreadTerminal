#if defined(_WIN32)

#include "termcore/profile.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;

namespace termcore {

static Profile makeProfile(const std::string& id, const std::string& name,
                            const std::string& command, const std::string& icon,
                            const std::vector<std::string>& args = {}) {
    Profile p;
    p.id = id; p.name = name; p.command = command; p.icon = icon;
    p.args = args; p.auto_detected = true;
    return p;
}

static std::string getEnv(const char* name) {
    const char* val = std::getenv(name);
    return val ? val : "";
}

static std::string runCommand(const std::string& cmd, int timeout_ms = 3000) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr, write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return "";
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::string cmdline = cmd;
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(read_pipe); CloseHandle(write_pipe);
        return "";
    }
    CloseHandle(write_pipe);

    DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(read_pipe);
        return "";
    }

    std::string output;
    char buf[4096];
    DWORD bytes_read = 0;
    while (ReadFile(read_pipe, buf, sizeof(buf), &bytes_read, nullptr) && bytes_read > 0) {
        output.append(buf, bytes_read);
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(read_pipe);
    return output;
}

static std::string utf16ToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), result.data(), size, nullptr, nullptr);
    return result;
}

static std::vector<Profile> detectWslDistros() {
    std::vector<Profile> result;
    std::string raw = runCommand("wsl --list --quiet", 3000);
    if (raw.empty()) return result;

    std::wstring wide;
    if (raw.size() >= 2) {
        const auto* data = reinterpret_cast<const wchar_t*>(raw.data());
        size_t wlen = raw.size() / sizeof(wchar_t);
        if (wlen > 0 && data[0] == 0xFEFF) wide.assign(data + 1, wlen - 1);
        else wide.assign(data, wlen);
    }
    std::string utf8 = utf16ToUtf8(wide);

    std::istringstream iss(utf8);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
               line.back() == ' ' || line.back() == '\0'))
            line.pop_back();
        if (line.empty()) continue;

        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        result.push_back(makeProfile("wsl-" + lower, line + " (WSL)", "wsl", "wsl", {"-d", line}));
    }
    return result;
}

std::vector<Profile> detectWindowsShells() {
    std::vector<Profile> profiles;
    std::string sysRoot = getEnv("SystemRoot");

    // 1. cmd.exe
    char sysDir[MAX_PATH] = {};
    GetSystemDirectoryA(sysDir, MAX_PATH);
    profiles.push_back(makeProfile("cmd", "cmd.exe", std::string(sysDir) + "\\cmd.exe", "cmd"));

    // 2. PowerShell 5
    if (!sysRoot.empty()) {
        std::string ps5 = sysRoot + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        if (fs::exists(ps5)) {
            profiles.push_back(makeProfile("powershell", "Windows PowerShell", ps5, "powershell"));
        }
    }

    // 3. PowerShell 7+
    std::string progFiles = getEnv("ProgramFiles");
    if (!progFiles.empty()) {
        std::string pwsh = progFiles + "\\PowerShell\\7\\pwsh.exe";
        if (fs::exists(pwsh)) {
            profiles.push_back(makeProfile("pwsh", "PowerShell 7", pwsh, "powershell"));
        }
    }

    // 4. Git Bash
    if (!progFiles.empty()) {
        std::string gitBash = progFiles + "\\Git\\bin\\bash.exe";
        if (fs::exists(gitBash)) {
            profiles.push_back(makeProfile("git-bash", "Git Bash", gitBash, "bash"));
        }
    }

    // 5. WSL distros (3s timeout)
    auto wsl = detectWslDistros();
    profiles.insert(profiles.end(), std::make_move_iterator(wsl.begin()), std::make_move_iterator(wsl.end()));

    return profiles;
}

} // namespace termcore
#endif
