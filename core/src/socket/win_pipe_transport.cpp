#include "termcore/socket/socket_transport.h"

#ifdef _WIN32

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace termcore {

/// Named-pipe implementation of ISocketTransport for Windows.
///
/// Each connected client is assigned a synthetic integer "fd" that maps to a
/// HANDLE internally.  This keeps the SocketServer code (which uses int fd)
/// working without changes.
class WinPipeTransport : public ISocketTransport {
public:
    explicit WinPipeTransport(std::string path)
        : path_(std::move(path)) {}

    ~WinPipeTransport() override {
        shutdown();
    }

    bool listen() override {
        // Create the shutdown event used to unblock acceptClient()
        shutdown_event_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (shutdown_event_ == nullptr) return false;

        // Create the first pipe instance (the "listening" pipe)
        listen_pipe_ = createPipeInstance();
        return listen_pipe_ != INVALID_HANDLE_VALUE;
    }

    int acceptClient() override {
        if (listen_pipe_ == INVALID_HANDLE_VALUE) return -1;

        // Overlapped connect so we can cancel on shutdown
        OVERLAPPED ov{};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (ov.hEvent == nullptr) return -1;

        BOOL connected = ConnectNamedPipe(listen_pipe_, &ov);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                // Wait for either a client connection or shutdown
                HANDLE events[2] = { ov.hEvent, shutdown_event_ };
                DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (wait != WAIT_OBJECT_0) {
                    // Shutdown or error
                    CancelIo(listen_pipe_);
                    CloseHandle(ov.hEvent);
                    return -1;
                }
            } else if (err != ERROR_PIPE_CONNECTED) {
                CloseHandle(ov.hEvent);
                return -1;
            }
        }
        CloseHandle(ov.hEvent);

        // The current listen_pipe_ is now connected to a client.
        HANDLE client_handle = listen_pipe_;

        // Create a new pipe instance for the next client
        listen_pipe_ = createPipeInstance();

        // Assign a synthetic fd
        int fd = next_fd_.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(map_mutex_);
            handle_map_[fd] = client_handle;
        }
        return fd;
    }

    int readLine(int fd, std::string& out) override {
        HANDLE h = getHandle(fd);
        if (h == INVALID_HANDLE_VALUE) return -1;

        static constexpr size_t kMaxLineLength = 16 * 1024 * 1024;  // 16 MB cap

        std::lock_guard<std::mutex> lock(buf_mutex_);
        auto& buf = read_bufs_[fd];

        while (true) {
            // Check if we already have a complete line
            auto pos = buf.find('\n');
            if (pos != std::string::npos) {
                out = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                return 1;
            }

            if (buf.size() > kMaxLineLength) {
                read_bufs_.erase(fd);
                return -2;
            }

            char tmp[4096];
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(h, tmp, sizeof(tmp), &bytesRead, nullptr);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_MORE_DATA) {
                    buf.append(tmp, bytesRead);
                    continue;
                }
                read_bufs_.erase(fd);
                return (err == ERROR_BROKEN_PIPE) ? 0 : -1;
            }
            if (bytesRead == 0) {
                read_bufs_.erase(fd);
                return 0;
            }
            buf.append(tmp, bytesRead);
        }
    }

    bool writeLine(int fd, const std::string& line) override {
        HANDLE h = getHandle(fd);
        if (h == INVALID_HANDLE_VALUE) return false;

        std::string data = line + "\n";
        const char* ptr = data.c_str();
        DWORD remaining = static_cast<DWORD>(data.size());

        while (remaining > 0) {
            DWORD written = 0;
            if (!WriteFile(h, ptr, remaining, &written, nullptr)) {
                return false;
            }
            ptr += written;
            remaining -= written;
        }
        FlushFileBuffers(h);
        return true;
    }

    void closeClient(int fd) override {
        HANDLE h = INVALID_HANDLE_VALUE;
        {
            std::lock_guard<std::mutex> lock(map_mutex_);
            auto it = handle_map_.find(fd);
            if (it != handle_map_.end()) {
                h = it->second;
                handle_map_.erase(it);
            }
        }
        if (h != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(h);
            CloseHandle(h);
        }
        {
            std::lock_guard<std::mutex> lock(buf_mutex_);
            read_bufs_.erase(fd);
        }
    }

    void shutdown() override {
        if (shutdown_event_ != nullptr) {
            SetEvent(shutdown_event_);
        }
        if (listen_pipe_ != INVALID_HANDLE_VALUE) {
            // Cancel any pending ConnectNamedPipe
            CancelIo(listen_pipe_);
            CloseHandle(listen_pipe_);
            listen_pipe_ = INVALID_HANDLE_VALUE;
        }
        if (shutdown_event_ != nullptr) {
            CloseHandle(shutdown_event_);
            shutdown_event_ = nullptr;
        }
    }

    std::string socketPath() const override { return path_; }

private:
    HANDLE createPipeInstance() {
        return CreateNamedPipeA(
            path_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096,   // output buffer size
            4096,   // input buffer size
            0,      // default timeout
            nullptr // default security
        );
    }

    HANDLE getHandle(int fd) {
        std::lock_guard<std::mutex> lock(map_mutex_);
        auto it = handle_map_.find(fd);
        return (it != handle_map_.end()) ? it->second : INVALID_HANDLE_VALUE;
    }

    std::string path_;
    HANDLE listen_pipe_ = INVALID_HANDLE_VALUE;
    HANDLE shutdown_event_ = nullptr;

    std::atomic<int> next_fd_{1};
    std::mutex map_mutex_;
    std::unordered_map<int, HANDLE> handle_map_;

    std::mutex buf_mutex_;
    std::unordered_map<int, std::string> read_bufs_;
};

std::unique_ptr<ISocketTransport> createSocketTransport(const std::string& path) {
    return std::make_unique<WinPipeTransport>(path);
}

}  // namespace termcore

#endif  // _WIN32
