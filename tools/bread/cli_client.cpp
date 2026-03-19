#include "cli_client.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace bread {

bool CliClient::connect(const std::string& socket_path, int timeout_ms) {
#ifndef _WIN32
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
#else
    error_ = "Windows not yet supported";
    return false;
#endif
}

bool CliClient::sendRequest(const std::string& json_line, std::string& response) {
#ifndef _WIN32
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
#else
    error_ = "Windows not yet supported";
    return false;
#endif
}

void CliClient::close() {
#ifndef _WIN32
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

}  // namespace bread
