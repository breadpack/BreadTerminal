#ifndef TERMCORE_NOTIFICATION_VISUAL_H
#define TERMCORE_NOTIFICATION_VISUAL_H

#include "termcore/notification.h"
#include "termcore/mux.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// Visual notification state for a single pane.
struct PaneNotificationState {
    PaneId pane_id = 0;
    bool has_unread = false;        // unread notification exists
    bool needs_attention = false;   // agent needs input
    bool is_active = false;         // currently focused
    float ring_intensity = 0.0f;    // 0.0-1.0 for pulse animation
    uint32_t ring_color = 0x007acc; // blue accent by default
    std::string latest_text;        // latest notification text for sidebar
    std::chrono::steady_clock::time_point last_notification_time;
};

/// Tracks visual notification indicators per pane.
///
/// Designed to be ticked each frame for smooth pulse animations.
/// Connect to NotificationStore via its callback and to AgentTracker
/// via its state-change callback.
class NotificationVisualState {
public:
    NotificationVisualState() = default;
    ~NotificationVisualState() = default;

    /// Called when a new notification arrives for a pane.
    void onNotification(PaneId pane, const std::string& text);

    /// Called when an agent in the given pane transitions to NeedsInput.
    void onAgentNeedsInput(PaneId pane);

    /// Called when the user focuses a pane -- clears unread / attention flags.
    void onPaneFocused(PaneId pane);

    /// Advance pulse animations by @p dt seconds.
    void tick(float dt);

    /// Get the visual state for a pane (nullptr if no state tracked).
    const PaneNotificationState* getState(PaneId pane) const;

    /// Return all pane IDs that currently need attention.
    std::vector<PaneId> panesNeedingAttention() const;

    /// Check whether any pane in @p tab has unread notifications.
    bool tabHasUnread(TabId tab, const Mux& mux) const;

    /// Wire this instance into a NotificationStore so that every new
    /// notification automatically updates visual state.
    void connectToStore(NotificationStore& store);

    /// Wire this instance into an AgentTracker so that NeedsInput
    /// state transitions automatically trigger attention indicators.
    /// Requires the AgentStateCallback signature from agent.h.
    void connectToAgentTracker(class AgentTracker& tracker);

    /// Remove all tracked state for a pane (e.g. after pane close).
    void removePane(PaneId pane);

private:
    /// Lazily creates and returns the state entry for @p pane.
    PaneNotificationState& ensureState(PaneId pane);

    std::unordered_map<PaneId, PaneNotificationState> states_;

    // Pulse animation parameters
    static constexpr float kPulseSpeed = 3.0f;   // radians / second
    static constexpr float kPulseMin = 0.3f;
    static constexpr float kPulseMax = 1.0f;
    float pulse_phase_ = 0.0f;
};

} // namespace termcore

#endif // TERMCORE_NOTIFICATION_VISUAL_H
