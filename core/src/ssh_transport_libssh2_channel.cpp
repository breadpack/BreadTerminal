#include "termcore/ssh_transport_libssh2.h"

#if TERMCORE_HAS_LIBSSH2

namespace termcore {

// ---------------------------------------------------------------------------
// Channel management
// ---------------------------------------------------------------------------

int Libssh2Transport::openChannel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != SshTransportState::Authenticated || !session_) return -1;

    // Temporarily switch to blocking for channel open
    libssh2_session_set_blocking(session_, 1);

    LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(session_);
    if (!ch) {
        libssh2_session_set_blocking(session_, 0);
        return -1;
    }

    // Request PTY
    int rc = libssh2_channel_request_pty_ex(
        ch, config_.term_type.c_str(),
        static_cast<unsigned int>(config_.term_type.size()),
        nullptr, 0,  // modes
        config_.pty_cols, config_.pty_rows, 0, 0);
    if (rc != 0) {
        libssh2_channel_free(ch);
        libssh2_session_set_blocking(session_, 0);
        return -1;
    }

    // Start shell
    rc = libssh2_channel_shell(ch);
    if (rc != 0) {
        libssh2_channel_free(ch);
        libssh2_session_set_blocking(session_, 0);
        return -1;
    }

    libssh2_session_set_blocking(session_, 0);

    int id = next_channel_id_++;
    Libssh2Channel lcd;
    lcd.id = id;
    lcd.channel = ch;
    lcd.active = true;
    channels_[id] = std::move(lcd);
    return id;
}

bool Libssh2Transport::closeChannel(int channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return false;

    if (it->second.channel) {
        libssh2_session_set_blocking(session_, 1);
        libssh2_channel_send_eof(it->second.channel);
        libssh2_channel_close(it->second.channel);
        libssh2_channel_free(it->second.channel);
        libssh2_session_set_blocking(session_, 0);
    }
    channels_.erase(it);
    return true;
}

bool Libssh2Transport::writeToChannel(int channel_id, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end() || !it->second.active) return false;
    it->second.write_buffer += data;
    return true;
}

std::string Libssh2Transport::readFromChannel(int channel_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end() || !it->second.active) return {};
    std::string data;
    std::swap(data, it->second.read_buffer);
    return data;
}

// ---------------------------------------------------------------------------
// I/O polling
// ---------------------------------------------------------------------------

void Libssh2Transport::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_ || state_ != SshTransportState::Authenticated) return;

    char buf[4096];
    for (auto& [id, ch] : channels_) {
        if (!ch.active || !ch.channel) continue;

        // Read stdout
        for (;;) {
            int nread = libssh2_channel_read(ch.channel, buf, sizeof(buf));
            if (nread > 0) {
                ch.read_buffer.append(buf, nread);
            } else {
                break;
            }
        }

        // Read stderr
        for (;;) {
            int nread = libssh2_channel_read_stderr(ch.channel, buf, sizeof(buf));
            if (nread > 0) {
                ch.read_buffer.append(buf, nread);
            } else {
                break;
            }
        }

        // Flush write buffer
        while (!ch.write_buffer.empty()) {
            int nwritten = libssh2_channel_write(
                ch.channel, ch.write_buffer.data(), ch.write_buffer.size());
            if (nwritten > 0) {
                ch.write_buffer.erase(0, nwritten);
            } else if (nwritten == LIBSSH2_ERROR_EAGAIN) {
                break;
            } else {
                ch.active = false;
                break;
            }
        }

        // Check if channel closed by remote
        if (libssh2_channel_eof(ch.channel)) {
            ch.active = false;
        }
    }
}

void Libssh2Transport::sendKeepalive() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_ || state_ != SshTransportState::Authenticated) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_keepalive_);
    if (elapsed.count() >= config_.keepalive_seconds &&
        config_.keepalive_seconds > 0) {
        int seconds_to_next = 0;
        libssh2_keepalive_send(session_, &seconds_to_next);
        last_keepalive_ = now;
    }
}

bool Libssh2Transport::resizeChannel(int channel_id, int cols, int rows) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end() || !it->second.active || !it->second.channel) {
        return false;
    }

    libssh2_session_set_blocking(session_, 1);
    int rc = libssh2_channel_request_pty_size(it->second.channel, cols, rows);
    libssh2_session_set_blocking(session_, 0);
    return rc == 0;
}

} // namespace termcore

#endif // TERMCORE_HAS_LIBSSH2
