#include "termcore/ssh_transport_libssh2.h"

#if TERMCORE_HAS_LIBSSH2

#include <cstring>

#if defined(_WIN32)
#pragma comment(lib, "ws2_32.lib")
#endif

namespace termcore {

// ---------------------------------------------------------------------------
// Global init / shutdown
// ---------------------------------------------------------------------------

static std::atomic<int> g_libssh2_refcount{0};

void libssh2GlobalInit() {
    if (g_libssh2_refcount.fetch_add(1) == 0) {
#if defined(_WIN32)
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2, 2), &wsadata);
#endif
        libssh2_init(0);
    }
}

void libssh2GlobalShutdown() {
    if (g_libssh2_refcount.fetch_sub(1) == 1) {
        libssh2_exit();
#if defined(_WIN32)
        WSACleanup();
#endif
    }
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Libssh2Transport::Libssh2Transport(const SshTransportConfig& config)
    : config_(config) {
    libssh2GlobalInit();
}

Libssh2Transport::~Libssh2Transport() {
    disconnect();
    libssh2GlobalShutdown();
}

// ---------------------------------------------------------------------------
// State accessors
// ---------------------------------------------------------------------------

SshTransportState Libssh2Transport::state() const {
    return state_.load();
}

std::string Libssh2Transport::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

bool Libssh2Transport::isConnected() const {
    return state_.load() == SshTransportState::Authenticated;
}

int Libssh2Transport::activeChannelCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (const auto& [id, ch] : channels_) {
        if (ch.active) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Error helpers
// ---------------------------------------------------------------------------

void Libssh2Transport::setError(const std::string& msg) {
    last_error_ = msg;
    state_ = SshTransportState::Error;
}

void Libssh2Transport::setErrorFromSession(const std::string& prefix) {
    if (session_) {
        char* errmsg = nullptr;
        libssh2_session_last_error(session_, &errmsg, nullptr, 0);
        last_error_ = prefix + ": " + (errmsg ? errmsg : "unknown error");
    } else {
        last_error_ = prefix + ": no session";
    }
    state_ = SshTransportState::Error;
}

// ---------------------------------------------------------------------------
// TCP socket connection
// ---------------------------------------------------------------------------

bool Libssh2Transport::connectSocket() {
    state_ = SshTransportState::Connecting;

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(config_.port);
    struct addrinfo* res = nullptr;
    int rc = getaddrinfo(config_.hostname.c_str(), port_str.c_str(),
                         &hints, &res);
    if (rc != 0 || !res) {
        setError("DNS resolution failed for " + config_.hostname);
        return false;
    }

    socket_ = kInvalidSocket;
    for (auto* rp = res; rp; rp = rp->ai_next) {
        socket_t s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == kInvalidSocket) continue;

#if defined(_WIN32)
        DWORD timeout_ms = config_.connect_timeout_seconds * 1000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
        struct timeval tv;
        tv.tv_sec = config_.connect_timeout_seconds;
        tv.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        if (::connect(s, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
            socket_ = s;
            break;
        }
#if defined(_WIN32)
        closesocket(s);
#else
        close(s);
#endif
    }
    freeaddrinfo(res);

    if (socket_ == kInvalidSocket) {
        setError("TCP connection failed to " + config_.hostname + ":" +
                 std::to_string(config_.port));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SSH handshake
// ---------------------------------------------------------------------------

bool Libssh2Transport::performHandshake() {
    state_ = SshTransportState::Handshaking;

    session_ = libssh2_session_init();
    if (!session_) {
        setError("Failed to create libssh2 session");
        return false;
    }

    libssh2_session_set_blocking(session_, 1);

    int rc = libssh2_session_handshake(session_, static_cast<int>(socket_));
    if (rc != 0) {
        setErrorFromSession("SSH handshake failed");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Host key verification (TOFU)
// ---------------------------------------------------------------------------

bool Libssh2Transport::verifyHostKey() {
    state_ = SshTransportState::HostKeyVerify;

    size_t len = 0;
    int type = 0;
    const char* fingerprint = libssh2_session_hostkey(session_, &len, &type);
    if (!fingerprint) {
        setError("Could not retrieve host key");
        return false;
    }

    std::string key_hex;
    key_hex.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x",
                 static_cast<unsigned char>(fingerprint[i]));
        key_hex += buf;
    }

    std::string key_type;
    switch (type) {
        case LIBSSH2_HOSTKEY_TYPE_RSA:       key_type = "ssh-rsa"; break;
        case LIBSSH2_HOSTKEY_TYPE_DSS:       key_type = "ssh-dss"; break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: key_type = "ecdsa-sha2-nistp256"; break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: key_type = "ecdsa-sha2-nistp384"; break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: key_type = "ecdsa-sha2-nistp521"; break;
        case LIBSSH2_HOSTKEY_TYPE_ED25519:   key_type = "ssh-ed25519"; break;
        default: key_type = "unknown"; break;
    }

    auto result = known_hosts_.check(config_.hostname, config_.port,
                                      key_type, key_hex);
    switch (result) {
        case KnownHostResult::Match:
            return true;
        case KnownHostResult::Mismatch:
            setError("Host key mismatch for " + config_.hostname +
                     " - possible MITM attack");
            return false;
        case KnownHostResult::NotFound:
        case KnownHostResult::Error:
            known_hosts_.addEntry(config_.hostname, config_.port,
                                  key_type, key_hex);
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Authentication
// ---------------------------------------------------------------------------

bool Libssh2Transport::tryAuthAgent() {
    LIBSSH2_AGENT* agent = libssh2_agent_init(session_);
    if (!agent) return false;

    bool success = false;
    if (libssh2_agent_connect(agent) == 0) {
        if (libssh2_agent_list_identities(agent) == 0) {
            struct libssh2_agent_publickey* identity = nullptr;
            struct libssh2_agent_publickey* prev = nullptr;
            while (libssh2_agent_get_identity(agent, &identity, prev) == 0) {
                if (libssh2_agent_userauth(agent, config_.username.c_str(),
                                           identity) == 0) {
                    success = true;
                    break;
                }
                prev = identity;
            }
        }
        libssh2_agent_disconnect(agent);
    }
    libssh2_agent_free(agent);
    return success;
}

bool Libssh2Transport::tryAuthPublicKey(const std::string& key_path) {
    std::string pub_path = key_path + ".pub";
    int rc = libssh2_userauth_publickey_fromfile(
        session_, config_.username.c_str(),
        pub_path.c_str(), key_path.c_str(), nullptr);
    return rc == 0;
}

bool Libssh2Transport::tryAuthPassword() {
    int rc = libssh2_userauth_password(session_, config_.username.c_str(),
                                       config_.password.c_str());
    return rc == 0;
}

bool Libssh2Transport::authenticate() {
    state_ = SshTransportState::Authenticating;

    auto methods = selectAuthMethods(config_);
    if (methods.empty()) {
        setError("No authentication methods available");
        return false;
    }

    char* auth_list = libssh2_userauth_list(session_, config_.username.c_str(),
                                             static_cast<unsigned int>(
                                                 config_.username.size()));
    std::string server_methods = auth_list ? auth_list : "";

    for (auto method : methods) {
        bool ok = false;
        switch (method) {
            case SshAuthMethod::Agent:
                ok = tryAuthAgent();
                break;
            case SshAuthMethod::PublicKey:
                if (server_methods.find("publickey") == std::string::npos &&
                    !server_methods.empty()) continue;
                if (!config_.identity_files.empty()) {
                    for (const auto& key : config_.identity_files) {
                        ok = tryAuthPublicKey(key);
                        if (ok) break;
                    }
                } else {
                    for (const auto& key : defaultIdentityFiles()) {
                        ok = tryAuthPublicKey(key);
                        if (ok) break;
                    }
                }
                break;
            case SshAuthMethod::Password:
                if (server_methods.find("password") == std::string::npos &&
                    !server_methods.empty()) continue;
                ok = tryAuthPassword();
                break;
            case SshAuthMethod::None:
                break;
        }
        if (ok) {
            state_ = SshTransportState::Authenticated;
            return true;
        }
    }

    setError("All authentication methods failed");
    return false;
}

// ---------------------------------------------------------------------------
// Full connection sequence
// ---------------------------------------------------------------------------

bool Libssh2Transport::connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connectSocket()) return false;
    if (!performHandshake()) { closeSocket(); return false; }
    if (!verifyHostKey()) { cleanupSession(); closeSocket(); return false; }
    if (!authenticate()) { cleanupSession(); closeSocket(); return false; }

    libssh2_session_set_blocking(session_, 0);

    if (config_.keepalive_seconds > 0) {
        libssh2_keepalive_config(session_, 1, config_.keepalive_seconds);
    }

    last_keepalive_ = std::chrono::steady_clock::now();
    return true;
}

// ---------------------------------------------------------------------------
// Disconnect and cleanup
// ---------------------------------------------------------------------------

void Libssh2Transport::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [id, ch] : channels_) {
        if (ch.channel) {
            libssh2_channel_send_eof(ch.channel);
            libssh2_channel_close(ch.channel);
            libssh2_channel_free(ch.channel);
            ch.channel = nullptr;
        }
        ch.active = false;
    }
    channels_.clear();

    cleanupSession();
    closeSocket();
    state_ = SshTransportState::Disconnected;
}

void Libssh2Transport::cleanupSession() {
    if (session_) {
        libssh2_session_disconnect(session_, "Normal shutdown");
        libssh2_session_free(session_);
        session_ = nullptr;
    }
}

void Libssh2Transport::closeSocket() {
    if (socket_ != kInvalidSocket) {
#if defined(_WIN32)
        closesocket(socket_);
#else
        close(socket_);
#endif
        socket_ = kInvalidSocket;
    }
}

} // namespace termcore

#endif // TERMCORE_HAS_LIBSSH2
