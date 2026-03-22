#include "termcore/ssh_mux.h"

#include <algorithm>

namespace termcore {

// ---------------------------------------------------------------------------
// Session key generation (shared by both implementations)
// ---------------------------------------------------------------------------

std::string SshMuxSession::makeSessionKey(const SshConfig& config) {
    std::string host = config.hostname.empty() ? config.host : config.hostname;
    std::string user = config.user.empty() ? "default" : config.user;
    return user + "@" + host + ":" + std::to_string(config.port);
}

// ===========================================================================
// Real libssh2 implementation
// ===========================================================================

#if TERMCORE_HAS_LIBSSH2

SshMuxSession::SshMuxSession(const SshConfig& config)
    : config_(config) {
    SshTransportConfig tc;
    tc.hostname = config.hostname.empty() ? config.host : config.hostname;
    tc.port = config.port;
    tc.username = config.user;
    if (!config.identity_file.empty()) {
        tc.identity_files.push_back(config.identity_file);
    }
    transport_ = std::make_unique<Libssh2Transport>(tc);
}

SshMuxSession::~SshMuxSession() = default;

bool SshMuxSession::connect() {
    if (!transport_) return false;
    return transport_->connect();
}

int SshMuxSession::openChannel() {
    if (!transport_) return -1;
    return transport_->openChannel();
}

bool SshMuxSession::closeChannel(int channel_id) {
    if (!transport_) return false;
    return transport_->closeChannel(channel_id);
}

bool SshMuxSession::writeToChannel(int channel_id, const std::string& data) {
    if (!transport_) return false;
    return transport_->writeToChannel(channel_id, data);
}

std::string SshMuxSession::readFromChannel(int channel_id) {
    if (!transport_) return {};
    return transport_->readFromChannel(channel_id);
}

void SshMuxSession::poll() {
    if (!transport_) return;
    transport_->poll();
    transport_->sendKeepalive();
}

std::vector<SshChannel> SshMuxSession::listChannels() const {
    // The transport doesn't expose a list; we'd need to track here.
    // For now, return empty. The manager tracks channels by id.
    return {};
}

int SshMuxSession::activeChannelCount() const {
    if (!transport_) return 0;
    return transport_->activeChannelCount();
}

bool SshMuxSession::isConnected() const {
    if (!transport_) return false;
    return transport_->isConnected();
}

SshTransportState SshMuxSession::transportState() const {
    if (!transport_) return SshTransportState::Disconnected;
    return transport_->state();
}

std::string SshMuxSession::lastError() const {
    if (!transport_) return "no transport";
    return transport_->lastError();
}

// ===========================================================================
// Stub implementation (no libssh2)
// ===========================================================================

#else

SshMuxSession::SshMuxSession(const SshConfig& config)
    : config_(config) {}

SshMuxSession::~SshMuxSession() = default;

bool SshMuxSession::connect() {
    // Stub: always succeeds
    return true;
}

int SshMuxSession::openChannel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return -1;

    int id = next_channel_id_++;
    ChannelData cd;
    cd.info.channel_id = id;
    cd.info.shell = "/bin/sh";
    cd.info.active = true;
    channels_[id] = std::move(cd);
    return id;
}

bool SshMuxSession::closeChannel(int channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return false;
    it->second.info.active = false;
    channels_.erase(it);
    return true;
}

bool SshMuxSession::writeToChannel(int channel_id, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return false;
    auto it = channels_.find(channel_id);
    if (it == channels_.end() || !it->second.info.active) return false;
    it->second.write_buffer += data;
    return true;
}

std::string SshMuxSession::readFromChannel(int channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end() || !it->second.info.active) return {};
    std::string data;
    std::swap(data, it->second.read_buffer);
    return data;
}

void SshMuxSession::poll() {
    // Stub: no-op
}

std::vector<SshChannel> SshMuxSession::listChannels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SshChannel> result;
    result.reserve(channels_.size());
    for (const auto& [id, cd] : channels_) {
        result.push_back(cd.info);
    }
    return result;
}

int SshMuxSession::activeChannelCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (const auto& [id, cd] : channels_) {
        if (cd.info.active) ++count;
    }
    return count;
}

bool SshMuxSession::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

SshTransportState SshMuxSession::transportState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ ? SshTransportState::Authenticated
                      : SshTransportState::Disconnected;
}

std::string SshMuxSession::lastError() const {
    return {};
}

#endif // TERMCORE_HAS_LIBSSH2

} // namespace termcore
