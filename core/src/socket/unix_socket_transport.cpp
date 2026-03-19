#include "termcore/socket/socket_transport.h"

#ifndef _WIN32

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace termcore {

class UnixSocketTransport : public ISocketTransport {
public:
    explicit UnixSocketTransport(std::string path)
        : path_(std::move(path)) {
        wakeup_pipe_[0] = -1;
        wakeup_pipe_[1] = -1;
    }

    ~UnixSocketTransport() override {
        shutdown();
    }

    bool listen() override {
        // Create self-pipe for clean shutdown signaling
        if (::pipe(wakeup_pipe_) < 0) return false;
        // Set non-blocking on read end
        int flags = ::fcntl(wakeup_pipe_[0], F_GETFL, 0);
        ::fcntl(wakeup_pipe_[0], F_SETFL, flags | O_NONBLOCK);

        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0) return false;

        // Remove stale socket file
        ::unlink(path_.c_str());

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path_.size() >= sizeof(addr.sun_path)) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
                   sizeof(addr)) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        // Restrict to owner only
        ::fchmod(listen_fd_, 0600);
        ::chmod(path_.c_str(), 0600);

        if (::listen(listen_fd_, 16) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            ::unlink(path_.c_str());
            return false;
        }

        return true;
    }

    int acceptClient() override {
        if (listen_fd_ < 0) return -1;

        struct pollfd fds[2];
        fds[0].fd = listen_fd_;
        fds[0].events = POLLIN;
        fds[1].fd = wakeup_pipe_[0];
        fds[1].events = POLLIN;

        int ret = ::poll(fds, 2, -1);
        if (ret < 0) return -1;

        // Check wakeup pipe (shutdown signal)
        if (fds[1].revents & POLLIN) {
            return -1;
        }

        if (fds[0].revents & POLLIN) {
            int client_fd = ::accept(listen_fd_, nullptr, nullptr);
            return client_fd;
        }

        return -1;
    }

    int readLine(int fd, std::string& out) override {
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

            char tmp[4096];
            ssize_t n = ::read(fd, tmp, sizeof(tmp));
            if (n < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            if (n == 0) {
                // EOF
                read_bufs_.erase(fd);
                return 0;
            }
            buf.append(tmp, static_cast<size_t>(n));
        }
    }

    bool writeLine(int fd, const std::string& line) override {
        std::string data = line + "\n";
        const char* ptr = data.c_str();
        size_t remaining = data.size();

        while (remaining > 0) {
            ssize_t n = ::write(fd, ptr, remaining);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            ptr += n;
            remaining -= static_cast<size_t>(n);
        }
        return true;
    }

    void closeClient(int fd) override {
        ::close(fd);
        std::lock_guard<std::mutex> lock(buf_mutex_);
        read_bufs_.erase(fd);
    }

    void shutdown() override {
        if (wakeup_pipe_[1] >= 0) {
            char c = 1;
            (void)::write(wakeup_pipe_[1], &c, 1);
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            ::unlink(path_.c_str());
        }
        if (wakeup_pipe_[0] >= 0) {
            ::close(wakeup_pipe_[0]);
            wakeup_pipe_[0] = -1;
        }
        if (wakeup_pipe_[1] >= 0) {
            ::close(wakeup_pipe_[1]);
            wakeup_pipe_[1] = -1;
        }
    }

    std::string socketPath() const override { return path_; }

private:
    std::string path_;
    int listen_fd_ = -1;
    int wakeup_pipe_[2];

    std::mutex buf_mutex_;
    std::unordered_map<int, std::string> read_bufs_;
};

std::unique_ptr<ISocketTransport> createSocketTransport(const std::string& path) {
    return std::make_unique<UnixSocketTransport>(path);
}

}  // namespace termcore

#endif  // !_WIN32
