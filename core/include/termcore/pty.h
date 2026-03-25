#ifndef TERMCORE_PTY_H
#define TERMCORE_PTY_H

#include <memory>
#include <string>
#include <vector>

namespace termcore {

/// PTY abstraction interface
class Pty {
public:
    virtual ~Pty() = default;

    /// Spawn a process in the PTY. Returns true on success.
    /// If command is empty, spawns the user's default shell.
    /// env_vars: additional environment variables to set (key=value pairs).
    virtual bool spawn(const std::string& command = "",
                       const std::vector<std::string>& args = {},
                       const std::string& working_dir = "",
                       int rows = 24, int cols = 80,
                       const std::vector<std::pair<std::string, std::string>>& env_vars = {}) = 0;

    /// Read from PTY (non-blocking). Returns bytes read, 0 if nothing
    /// available, -1 on error/closed.
    virtual int read(char* buf, size_t buf_size) = 0;

    /// Write to PTY. Returns bytes written, -1 on error.
    virtual int write(const char* data, size_t len) = 0;

    /// Resize the PTY.
    virtual void resize(int rows, int cols) = 0;

    /// Check if the child process is still running.
    virtual bool isAlive() const = 0;

    /// Get the child process PID (Unix) or handle.
    virtual int pid() const = 0;

    /// Get the PTY file descriptor for polling (Unix).
    virtual int fd() const = 0;

    /// Get the native read handle for event-driven polling (Windows).
    /// Returns nullptr on platforms that use fd() instead.
    virtual void* nativeReadHandle() const { return nullptr; }

    /// Wait for child process to exit. Returns exit code.
    virtual int waitForExit() = 0;

    /// Send signal to child process.
    virtual void signal(int sig) = 0;

    /// Get the foreground (deepest child) process name.
    /// Returns empty string if not available.
    virtual std::string foregroundProcessName() const { return {}; }

    /// Get the current working directory of the foreground process.
    /// Returns empty string if not available.
    virtual std::string foregroundCwd() const { return {}; }
};

/// Factory function
std::unique_ptr<Pty> createPty();

} // namespace termcore

#endif // TERMCORE_PTY_H
