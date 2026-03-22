#include "termcore/ssh_mux.h"

#include <algorithm>

namespace termcore {

// ---------------------------------------------------------------------------
// SshMuxSession
// ---------------------------------------------------------------------------

SshMuxSession::SshMuxSession(const SshConfig& config)
    : config_(config) {}

SshMuxSession::~SshMuxSession() = default;

std::string SshMuxSession::makeSessionKey(const SshConfig& config) {
    // Use the resolved hostname (falling back to the alias) so that
    // two configs pointing at the same machine share a connection.
    std::string host = config.hostname.empty() ? config.host : config.hostname;
    std::string user = config.user.empty() ? "default" : config.user;
    return user + "@" + host + ":" + std::to_string(config.port);
}

int SshMuxSession::openChannel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_) return -1;

    int id = next_channel_id_++;
    ChannelData cd;
    cd.info.channel_id = id;
    cd.info.shell = "/bin/sh"; // stub default
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
    // Stub: buffer data locally; real implementation would send over SSH.
    it->second.write_buffer += data;
    return true;
}

std::string SshMuxSession::readFromChannel(int channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end() || !it->second.info.active) return {};
    // Stub: return and clear the read buffer.
    std::string data;
    std::swap(data, it->second.read_buffer);
    return data;
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

} // namespace termcore
