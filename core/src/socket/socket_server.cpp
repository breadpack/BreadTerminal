#include "termcore/socket/socket_server.h"
#include "termcore/socket/jsonrpc.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace termcore {

/// Constant-time string comparison to prevent timing side-channel attacks.
static bool constantTimeEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    volatile unsigned char result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return result == 0;
}

SocketServer::SocketServer(std::unique_ptr<ISocketTransport> transport,
                           std::shared_ptr<CommandDispatcher> dispatcher)
    : transport_(std::move(transport))
    , dispatcher_(std::move(dispatcher))
{
}

SocketServer::~SocketServer() {
    stop();
}

bool SocketServer::start() {
    if (running_.load()) return false;

    if (!transport_->listen()) {
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void SocketServer::stop() {
    if (!running_.load()) return;
    running_.store(false);
    transport_->shutdown();
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void SocketServer::setAuthToken(std::string token) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    auth_token_ = std::move(token);
}

void SocketServer::drainMainThreadQueue() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        tasks.swap(pending_);
    }
    for (auto& fn : tasks) {
        fn();
    }
}

void SocketServer::acceptLoop() {
    while (running_.load()) {
        int client_fd = transport_->acceptClient();
        if (client_fd < 0) {
            // Avoid busy-spin on persistent errors; sleep briefly before retrying.
            if (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }
        // Spawn a detached thread per client
        std::thread([this, client_fd] {
            clientLoop(client_fd);
        }).detach();
    }
}

void SocketServer::clientLoop(int fd) {
    while (running_.load()) {
        std::string line;
        int rc = transport_->readLine(fd, line);
        if (rc <= 0) {
            // EOF or error
            break;
        }

        if (line.empty()) continue;

        // Parse JSON-RPC request
        std::string parseErr;
        auto req = rpc::parseRequest(line, &parseErr);

        rpc::Response resp;

        if (req.method.empty()) {
            // Parse error
            resp = rpc::makeError(std::nullopt, rpc::kParseError,
                                  parseErr.empty() ? "Parse error" : parseErr);
        } else {
            // Check auth token
            bool auth_ok = true;
            {
                std::lock_guard<std::mutex> lock(auth_mutex_);
                if (!auth_token_.empty()) {
                    if (!req.params.contains("_auth") ||
                        !req.params["_auth"].is_string() ||
                        !constantTimeEqual(req.params["_auth"].get<std::string>(), auth_token_)) {
                        auth_ok = false;
                    } else {
                        // Strip _auth before dispatching
                        req.params.erase("_auth");
                    }
                }
            }

            if (!auth_ok) {
                resp = rpc::makeError(req.id, rpc::kPermissionDenied,
                                      "Invalid or missing auth token");
            } else {
                // Dispatch with serialization mutex
                std::lock_guard<std::mutex> lock(dispatch_mutex_);
                resp = dispatcher_->dispatch(req);
            }
        }

        // Send response
        auto json_line = rpc::serializeResponse(resp);
        if (!transport_->writeLine(fd, json_line)) {
            break;
        }
    }

    transport_->closeClient(fd);
}

std::string resolveSocketPath() {
    const char* env = std::getenv("BREADTERMINAL_SOCKET");
    if (env && env[0] != '\0') {
        return env;
    }

#if defined(_WIN32)
    return "\\\\.\\pipe\\breadterminal";
#else
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/breadterminal.sock";
    }
    // Fallback: per-user path to avoid world-writable /tmp
    return "/tmp/breadterminal-" + std::to_string(getuid()) + ".sock";
#endif
}

}  // namespace termcore
