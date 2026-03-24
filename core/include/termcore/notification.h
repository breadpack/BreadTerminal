#ifndef TERMCORE_NOTIFICATION_H
#define TERMCORE_NOTIFICATION_H

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace termcore {

using PaneId = uint32_t; // Forward-compatible with mux.h

/// Notification urgency levels
enum class NotificationUrgency : uint8_t {
    Low,      // Informational
    Normal,   // Standard notification
    Critical, // Needs immediate attention (e.g., agent needs input)
};

/// Source of the notification
enum class NotificationSource : uint8_t {
    OSC9,   // ConEmu-style
    OSC99,  // Kitty-style
    OSC777, // rxvt-unicode style
    Agent,  // Agent hook system
    System, // Internal system event (process exit, etc.)
};

/// A single notification
struct Notification {
    uint64_t id;
    PaneId pane_id;
    NotificationSource source;
    NotificationUrgency urgency;
    std::string title;
    std::string body;
    std::chrono::steady_clock::time_point timestamp;
    bool read = false;
};

/// Callback for new notifications
using NotificationCallback = std::function<void(const Notification&)>;

/// Manages a list of notifications with filtering and lifecycle
class NotificationStore {
public:
    explicit NotificationStore(size_t max_notifications = 100);
    ~NotificationStore() = default;

    /// Add a notification. Returns its ID.
    uint64_t add(PaneId pane_id, NotificationSource source,
                 NotificationUrgency urgency, const std::string& title,
                 const std::string& body);

    /// Add notification from an OSC event
    uint64_t addFromOsc(PaneId pane_id, int osc_number,
                        const std::string& data);

    /// Mark a notification as read
    void markRead(uint64_t id);

    /// Mark all notifications as read
    void markAllRead();

    /// Remove a specific notification
    void remove(uint64_t id);

    /// Clear all notifications
    void clear();

    /// Clear notifications for a specific pane
    void clearForPane(PaneId pane_id);

    /// Get all notifications (newest first)
    const std::deque<Notification>& all() const { return notifications_; }

    /// Get unread notifications
    std::vector<const Notification*> unread() const;

    /// Get unread count
    size_t unreadCount() const;

    /// Get notifications for a specific pane
    std::vector<const Notification*> forPane(PaneId pane_id) const;

    /// Check if a pane has unread notifications
    bool hasUnread(PaneId pane_id) const;

    /// Set callback for new notifications
    void setCallback(NotificationCallback cb) { callback_ = std::move(cb); }

    /// Set maximum notifications limit (Lua-configurable).
    void setMaxNotifications(size_t n) { max_notifications_ = n; }

    /// Set deduplication window in seconds (Lua-configurable).
    /// Notifications with the same title+body within this window are suppressed.
    void setDeduplicateWindowSec(int seconds) { deduplicate_window_sec_ = seconds; }

    /// Get deduplication window in seconds.
    int deduplicateWindowSec() const { return deduplicate_window_sec_; }

    /// Total count
    size_t count() const { return notifications_.size(); }

private:
    std::deque<Notification> notifications_;
    size_t max_notifications_;
    uint64_t next_id_ = 1;
    NotificationCallback callback_;
    int deduplicate_window_sec_ = 0;

    /// Parse OSC notification data
    void parseOsc9(const std::string& data, std::string& title,
                   std::string& body);
    void parseOsc777(const std::string& data, std::string& title,
                     std::string& body);
    void parseOsc99(const std::string& data, std::string& title,
                    std::string& body);
};

} // namespace termcore

#endif // TERMCORE_NOTIFICATION_H
