#include "termcore/ssh_mux_manager.h"

#include <algorithm>

namespace termcore {

SshMuxManager::SshMuxManager() = default;
SshMuxManager::~SshMuxManager() = default;

std::shared_ptr<SshMuxSession> SshMuxManager::getOrCreateSession(const SshConfig& config) {
    std::string key = SshMuxSession::makeSessionKey(config);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(key);
    if (it != sessions_.end() && it->second->isConnected()) {
        return it->second;
    }

    auto session = std::make_shared<SshMuxSession>(config);
    sessions_[key] = session;
    return session;
}

bool SshMuxManager::closeSession(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return false;

    // Close all remaining channels.
    auto channels = it->second->listChannels();
    for (const auto& ch : channels) {
        it->second->closeChannel(ch.channel_id);
    }

    sessions_.erase(it);
    return true;
}

std::vector<SshMuxSessionInfo> SshMuxManager::listSessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SshMuxSessionInfo> result;
    result.reserve(sessions_.size());
    for (const auto& [key, session] : sessions_) {
        SshMuxSessionInfo info;
        info.key = key;
        info.display_name = session->config().displayName();
        info.channel_count = session->activeChannelCount();
        result.push_back(std::move(info));
    }
    return result;
}

void SshMuxManager::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second->activeChannelCount() == 0) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t SshMuxManager::sessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

} // namespace termcore
