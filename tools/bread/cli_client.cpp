#include "cli_client.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace bread {

#ifdef _WIN32

// --- Windows Named Pipe implementation ---

bool CliClient::connect(const std::string& socket_path, int timeout_ms) {
    // The socket_path is used as the named pipe path directly.
    // Expected format: \\.\pipe\breadterminal  (or similar)
    std::string pipe_name = socket_path;

    // Try to connect, waiting if the pipe is busy
    while (true) {
        pipe_ = CreateFileA(
            pipe_name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,              // no sharing
            nullptr,        // default security
            OPEN_EXISTING,
            0,              // default attributes
            nullptr         // no template file
        );

        if (pipe_ != INVALID_HANDLE_VALUE) {
            break;  // connected
        }

        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            error_ = "BreadTerminal is not running (pipe not found at " + pipe_name + ")";
            return false;
        }
        if (err != ERROR_PIPE_BUSY) {
            error_ = "Failed to connect to pipe: error code " + std::to_string(err);
            return false;
        }

        // Pipe is busy, wait for it to become available
        if (!WaitNamedPipeA(pipe_name.c_str(), static_cast<DWORD>(timeout_ms))) {
            error_ = "Timeout waiting for pipe: " + pipe_name;
            return false;
        }
    }

    // Configure a read timeout via COMMTIMEOUTS
    COMMTIMEOUTS timeouts{};
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(timeout_ms);
    SetCommTimeouts(pipe_, &timeouts);

    return true;
}

bool CliClient::sendRequest(const std::string& json_line, std::string& response) {
    if (pipe_ == INVALID_HANDLE_VALUE) {
        error_ = "Not connected";
        return false;
    }

    // Send request with newline
    std::string data = json_line + "\n";
    const char* ptr = data.c_str();
    DWORD remaining = static_cast<DWORD>(data.size());

    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile(pipe_, ptr, remaining, &written, nullptr)) {
            error_ = "Write failed: error code " + std::to_string(GetLastError());
            return false;
        }
        ptr += written;
        remaining -= written;
    }

    // Flush to ensure the server receives the data
    FlushFileBuffers(pipe_);

    // Read response until newline
    response.clear();
    char buf[4096];
    while (true) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(pipe_, buf, sizeof(buf), &bytesRead, nullptr);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_MORE_DATA) {
                // More data available, append what we got and continue
                response.append(buf, bytesRead);
                auto pos = response.find('\n');
                if (pos != std::string::npos) {
                    response.resize(pos);
                    break;
                }
                continue;
            }
            if (response.empty()) {
                error_ = "Read failed: error code " + std::to_string(err);
                return false;
            }
            break;
        }
        if (bytesRead == 0) {
            if (response.empty()) {
                error_ = "Connection closed by server";
                return false;
            }
            break;
        }
        response.append(buf, bytesRead);
        // Check for newline
        auto pos = response.find('\n');
        if (pos != std::string::npos) {
            response.resize(pos);  // Strip trailing newline
            break;
        }
    }

    return true;
}

void CliClient::close() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

#else

// --- Unix domain socket implementation ---

bool CliClient::connect(const std::string& socket_path, int timeout_ms) {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        error_ = "Failed to create socket: " + std::string(std::strerror(errno));
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        error_ = "Socket path too long";
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (errno == ENOENT || errno == ECONNREFUSED) {
            error_ = "BreadTerminal is not running (socket not found at " + socket_path + ")";
        } else {
            error_ = "Failed to connect: " + std::string(std::strerror(errno));
        }
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return true;
}

bool CliClient::sendRequest(const std::string& json_line, std::string& response) {
    if (fd_ < 0) {
        error_ = "Not connected";
        return false;
    }

    // Send request with newline
    std::string data = json_line + "\n";
    const char* ptr = data.c_str();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t n = ::write(fd_, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            error_ = "Write failed: " + std::string(std::strerror(errno));
            return false;
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }

    // Read response until newline
    response.clear();
    char buf[4096];
    while (true) {
        ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                error_ = "Timeout waiting for response";
                return false;
            }
            error_ = "Read failed: " + std::string(std::strerror(errno));
            return false;
        }
        if (n == 0) {
            if (response.empty()) {
                error_ = "Connection closed by server";
                return false;
            }
            break;
        }
        response.append(buf, static_cast<size_t>(n));
        // Check for newline
        auto pos = response.find('\n');
        if (pos != std::string::npos) {
            response.resize(pos);  // Strip trailing newline
            break;
        }
    }

    return true;
}

void CliClient::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

#endif  // _WIN32

}  // namespace bread
