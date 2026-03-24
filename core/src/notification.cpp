#include "termcore/notification.h"

#include <algorithm>
#include <sstream>

namespace termcore {

NotificationStore::NotificationStore(size_t max_notifications)
    : max_notifications_(max_notifications) {}

uint64_t NotificationStore::add(PaneId pane_id, NotificationSource source,
                                NotificationUrgency urgency,
                                const std::string& title,
                                const std::string& body) {
    // Deduplication: suppress if same title+body appeared within the window
    if (deduplicate_window_sec_ > 0) {
        auto now = std::chrono::steady_clock::now();
        for (const auto& existing : notifications_) {
            if (existing.title == title && existing.body == body) {
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - existing.timestamp).count();
                if (age < deduplicate_window_sec_) {
                    return existing.id; // Suppressed as duplicate
                }
            }
        }
    }

    Notification n;
    n.id = next_id_++;
    n.pane_id = pane_id;
    n.source = source;
    n.urgency = urgency;
    n.title = title;
    n.body = body;
    n.timestamp = std::chrono::steady_clock::now();
    n.read = false;

    notifications_.push_front(std::move(n));

    // Trim to max size — evict oldest (back of deque)
    while (notifications_.size() > max_notifications_) {
        notifications_.pop_back();
    }

    if (callback_) {
        callback_(notifications_.front());
    }

    return notifications_.front().id;
}

uint64_t NotificationStore::addFromOsc(PaneId pane_id, int osc_number,
                                       const std::string& data) {
    std::string title;
    std::string body;
    NotificationSource source;

    switch (osc_number) {
    case 9:
        source = NotificationSource::OSC9;
        parseOsc9(data, title, body);
        break;
    case 99:
        source = NotificationSource::OSC99;
        parseOsc99(data, title, body);
        break;
    case 777:
        source = NotificationSource::OSC777;
        parseOsc777(data, title, body);
        break;
    default:
        source = NotificationSource::System;
        title = "Unknown OSC";
        body = data;
        break;
    }

    return add(pane_id, source, NotificationUrgency::Normal, title, body);
}

void NotificationStore::markRead(uint64_t id) {
    for (auto& n : notifications_) {
        if (n.id == id) {
            n.read = true;
            return;
        }
    }
}

void NotificationStore::markAllRead() {
    for (auto& n : notifications_) {
        n.read = true;
    }
}

void NotificationStore::remove(uint64_t id) {
    auto it = std::remove_if(notifications_.begin(), notifications_.end(),
                             [id](const Notification& n) { return n.id == id; });
    notifications_.erase(it, notifications_.end());
}

void NotificationStore::clear() { notifications_.clear(); }

void NotificationStore::clearForPane(PaneId pane_id) {
    auto it = std::remove_if(
        notifications_.begin(), notifications_.end(),
        [pane_id](const Notification& n) { return n.pane_id == pane_id; });
    notifications_.erase(it, notifications_.end());
}

std::vector<const Notification*> NotificationStore::unread() const {
    std::vector<const Notification*> result;
    for (const auto& n : notifications_) {
        if (!n.read) {
            result.push_back(&n);
        }
    }
    return result;
}

size_t NotificationStore::unreadCount() const {
    size_t count = 0;
    for (const auto& n : notifications_) {
        if (!n.read) {
            ++count;
        }
    }
    return count;
}

std::vector<const Notification*>
NotificationStore::forPane(PaneId pane_id) const {
    std::vector<const Notification*> result;
    for (const auto& n : notifications_) {
        if (n.pane_id == pane_id) {
            result.push_back(&n);
        }
    }
    return result;
}

bool NotificationStore::hasUnread(PaneId pane_id) const {
    for (const auto& n : notifications_) {
        if (n.pane_id == pane_id && !n.read) {
            return true;
        }
    }
    return false;
}

void NotificationStore::parseOsc9(const std::string& data, std::string& title,
                                  std::string& body) {
    title = "Terminal Notification";
    body = data;
}

void NotificationStore::parseOsc777(const std::string& data,
                                    std::string& title, std::string& body) {
    // Format: "notify;title;body"
    std::istringstream ss(data);
    std::string command;

    if (std::getline(ss, command, ';')) {
        if (std::getline(ss, title, ';')) {
            std::getline(ss, body);
        } else {
            title = "Notification";
            body = data;
        }
    } else {
        title = "Notification";
        body = data;
    }

    if (title.empty()) {
        title = "Notification";
    }
}

void NotificationStore::parseOsc99(const std::string& data,
                                   std::string& title, std::string& body) {
    // Kitty format: key=value pairs separated by semicolons,
    // payload after colon. E.g. "i=1;d=0;p=title;body text"
    // Simplified: look for title (p=) and body after semicolons.
    //
    // Kitty notification protocol:
    //   The data before ':' contains key=value metadata separated by ';'
    //   The data after ':' is the payload (body).
    //   'p=title' means the payload is the title.

    title = "Notification";
    body.clear();

    auto colon_pos = data.find(':');
    std::string metadata;
    std::string payload;

    if (colon_pos != std::string::npos) {
        metadata = data.substr(0, colon_pos);
        payload = data.substr(colon_pos + 1);
    } else {
        // No colon — treat entire data as payload
        metadata.clear();
        payload = data;
    }

    // Parse metadata for p= (payload type)
    bool payload_is_title = false;
    std::istringstream ss(metadata);
    std::string token;
    while (std::getline(ss, token, ';')) {
        if (token == "p=title") {
            payload_is_title = true;
        }
    }

    if (payload_is_title) {
        title = payload;
    } else {
        body = payload;
    }
}

} // namespace termcore
