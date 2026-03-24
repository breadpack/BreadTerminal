#if !defined(_WIN32)

#include "termcore/pty.h"
#include "termcore/terminfo.h"
#include "termcore/shell_integration.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>       // macOS forkpty
#elif defined(__linux__)
#include <pty.h>        // Linux forkpty
#endif
#include <csignal>

namespace termcore {

class UnixPty : public Pty {
public:
    UnixPty() = default;

    ~UnixPty() override {
        cleanup();
    }

    bool spawn(const std::string& command,
               const std::vector<std::string>& args,
               const std::string& working_dir,
               int rows, int cols,
               const std::vector<std::pair<std::string, std::string>>& env_vars = {}) override {
        if (master_fd_ >= 0) {
            // Already spawned
            return false;
        }

        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(rows);
        ws.ws_col = static_cast<unsigned short>(cols);

        // Install terminfo entry once (static ensures single initialization)
        static auto s_terminfo = termcore::installTerminfo();

        // Suppress SIGPIPE so writes to a dead PTY return EPIPE
        // instead of killing the process.
        ::signal(SIGPIPE, SIG_IGN);

        pid_t child = forkpty(&master_fd_, nullptr, nullptr, &ws);
        if (child < 0) {
            master_fd_ = -1;
            return false;
        }

        if (child == 0) {
            // --- Child process ---
            setupChild(command, args, working_dir, s_terminfo, env_vars);
            // setupChild calls execvp; if we reach here, exec failed.
            _exit(127);
        }

        // --- Parent process ---
        child_pid_ = child;

        // Set non-blocking on master fd
        int flags = fcntl(master_fd_, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
        }

        return true;
    }

    int read(char* buf, size_t buf_size) override {
        if (master_fd_ < 0) {
            return -1;
        }
        ssize_t n = ::read(master_fd_, buf, buf_size);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            return -1;
        }
        if (n == 0) {
            // EOF — child closed its side
            return -1;
        }
        return static_cast<int>(n);
    }

    int write(const char* data, size_t len) override {
        if (master_fd_ < 0) {
            return -1;
        }
        ssize_t n = ::write(master_fd_, data, len);
        if (n < 0) {
            if (errno == EPIPE || errno == EIO) {
                // PTY slave closed — treat as graceful disconnect
                return -1;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            return -1;
        }
        return static_cast<int>(n);
    }

    void resize(int rows, int cols) override {
        if (master_fd_ < 0) {
            return;
        }
        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(rows);
        ws.ws_col = static_cast<unsigned short>(cols);
        ioctl(master_fd_, TIOCSWINSZ, &ws);
    }

    bool isAlive() const override {
        if (child_pid_ <= 0) {
            return false;
        }
        if (exited_) {
            return false;
        }
        int status = 0;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == 0) {
            // Still running
            return true;
        }
        if (result == child_pid_) {
            // Reaped — cache the result
            exited_ = true;
            if (WIFEXITED(status)) {
                exit_code_ = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code_ = 128 + WTERMSIG(status);
            }
            return false;
        }
        // Error (e.g., ECHILD)
        return false;
    }

    int pid() const override {
        return child_pid_;
    }

    int fd() const override {
        return master_fd_;
    }

    int waitForExit() override {
        if (child_pid_ <= 0) {
            return -1;
        }
        if (exited_) {
            return exit_code_;
        }
        int status = 0;
        pid_t result = waitpid(child_pid_, &status, 0);
        if (result == child_pid_) {
            exited_ = true;
            if (WIFEXITED(status)) {
                exit_code_ = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code_ = 128 + WTERMSIG(status);
            }
        }
        return exit_code_;
    }

    void signal(int sig) override {
        if (child_pid_ > 0 && !exited_) {
            kill(child_pid_, sig);
        }
    }

private:
    int master_fd_ = -1;
    pid_t child_pid_ = -1;
    mutable bool exited_ = false;
    mutable int exit_code_ = -1;

    void cleanup() {
        if (child_pid_ > 0 && !exited_) {
            kill(child_pid_, SIGTERM);
            // Give it a moment, then reap
            int status = 0;
            pid_t result = waitpid(child_pid_, &status, WNOHANG);
            if (result == 0) {
                // Still running — wait briefly then force kill
                usleep(50000); // 50ms
                kill(child_pid_, SIGKILL);
                waitpid(child_pid_, &status, 0);
            }
            exited_ = true;
        }
        if (master_fd_ >= 0) {
            close(master_fd_);
            master_fd_ = -1;
        }
    }

    static void setupChild(const std::string& command,
                            const std::vector<std::string>& args,
                            const std::string& working_dir,
                            const TerminfoInstallResult& terminfo,
                            const std::vector<std::pair<std::string, std::string>>& env_vars = {}) {
        // Change working directory (default to home directory)
        const char* dir = working_dir.empty() ? std::getenv("HOME") : working_dir.c_str();
        if (dir && dir[0]) {
            chdir(dir);  // ignore error; stay in current directory
        }

        // Set TERM environment variable (uses custom terminfo or fallback)
        setenv("TERM", terminfo.term_name.c_str(), 1);

        // Set TERMINFO path so the shell can find our custom entry
        if (!terminfo.terminfo_dir.empty()) {
            setenv("TERMINFO", terminfo.terminfo_dir.c_str(), 1);
        }

        // Additional environment for true color and program identification
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "BreadTerminal", 1);

        // SSH TERM fallback: shell integration scripts use this to downgrade
        // TERM when running ssh, since remote hosts won't have our terminfo.
        setenv("BREADTERMINAL_SSH_TERM", "xterm-256color", 0);

        // Shell integration environment variables
        for (const auto& [key, value] : getShellEnvVars()) {
            setenv(key.c_str(), value.c_str(), 1);
        }

        // Additional environment variables (e.g., pane environment for agents)
        for (const auto& [key, value] : env_vars) {
            setenv(key.c_str(), value.c_str(), 1);
        }

        // Ensure locale is set for proper UTF-8 handling (don't overwrite if already set)
        setenv("LANG", "en_US.UTF-8", 0);
        setenv("LC_CTYPE", "UTF-8", 0);

        // Determine the command to run
        std::string cmd = command;
        if (cmd.empty()) {
            const char* shell = getenv("SHELL");
            cmd = (shell && shell[0] != '\0') ? shell : "/bin/sh";
        }

        // Build argv
        std::vector<const char*> argv;
        argv.push_back(cmd.c_str());
        for (const auto& arg : args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        execvp(cmd.c_str(), const_cast<char* const*>(argv.data()));
        // If execvp returns, it failed
    }
};

std::unique_ptr<Pty> createPty() {
    return std::make_unique<UnixPty>();
}

} // namespace termcore

#endif // !defined(_WIN32)
