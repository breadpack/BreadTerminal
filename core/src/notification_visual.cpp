#include "termcore/notification_visual.h"
#include "termcore/agent.h"

#include <cmath>

namespace termcore {

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void NotificationVisualState::onNotification(PaneId pane,
                                              const std::string& text) {
    auto& s = ensureState(pane);
    s.has_unread = true;
    s.latest_text = text;
    s.last_notification_time = std::chrono::steady_clock::now();

    // Kick ring intensity to full so the pulse is immediately visible.
    s.ring_intensity = kPulseMax;
}

void NotificationVisualState::onAgentNeedsInput(PaneId pane) {
    auto& s = ensureState(pane);
    s.needs_attention = true;
    s.ring_color = 0xff8800; // orange for critical / needs-input
    s.ring_intensity = kPulseMax;
    s.last_notification_time = std::chrono::steady_clock::now();
}

void NotificationVisualState::onPaneFocused(PaneId pane) {
    auto& s = ensureState(pane);
    s.has_unread = false;
    s.needs_attention = false;
    s.is_active = true;
    s.ring_intensity = 0.0f;
    s.ring_color = 0x007acc; // reset to default blue

    // Mark all other panes as inactive.
    for (auto& [id, state] : states_) {
        if (id != pane) {
            state.is_active = false;
        }
    }
}

void NotificationVisualState::tick(float dt) {
    pulse_phase_ += kPulseSpeed * dt;
    if (pulse_phase_ > 6.2831853f) { // 2*pi
        pulse_phase_ -= 6.2831853f;
    }

    // Sine-based pulse between kPulseMin and kPulseMax.
    float pulse =
        kPulseMin + (kPulseMax - kPulseMin) * (0.5f + 0.5f * std::sin(pulse_phase_));

    for (auto& [id, s] : states_) {
        if (s.needs_attention) {
            s.ring_intensity = pulse;
        } else if (s.has_unread && !s.is_active) {
            // Gentle constant glow for unread-only.
            s.ring_intensity = 0.5f;
        } else {
            s.ring_intensity = 0.0f;
        }
    }
}

const PaneNotificationState*
NotificationVisualState::getState(PaneId pane) const {
    auto it = states_.find(pane);
    return (it != states_.end()) ? &it->second : nullptr;
}

std::vector<PaneId> NotificationVisualState::panesNeedingAttention() const {
    std::vector<PaneId> result;
    for (const auto& [id, s] : states_) {
        if (s.needs_attention) {
            result.push_back(id);
        }
    }
    return result;
}

bool NotificationVisualState::tabHasUnread(TabId tab, const Mux& mux) const {
    // We need a workspace that contains this tab.  Iterate all workspaces.
    for (auto ws_id : mux.allWorkspaceIds()) {
        for (auto tab_id : mux.allTabIds(ws_id)) {
            if (tab_id != tab) continue;
            auto panes = mux.allPanes(ws_id, tab_id);
            for (auto pane_id : panes) {
                auto it = states_.find(pane_id);
                if (it != states_.end() && it->second.has_unread) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Integration helpers
// ---------------------------------------------------------------------------

void NotificationVisualState::connectToStore(NotificationStore& store) {
    store.setCallback([this](const Notification& n) {
        std::string text = n.title;
        if (!n.body.empty()) {
            text += ": " + n.body;
        }
        onNotification(n.pane_id, text);

        // Critical urgency also triggers the attention flag.
        if (n.urgency == NotificationUrgency::Critical) {
            onAgentNeedsInput(n.pane_id);
        }
    });
}

void NotificationVisualState::connectToAgentTracker(AgentTracker& tracker) {
    tracker.setStateCallback(
        [this](uint32_t pane_id, const AgentInfo& info) {
            if (info.state == AgentState::NeedsInput) {
                onAgentNeedsInput(pane_id);
            }
        });
}

void NotificationVisualState::removePane(PaneId pane) {
    states_.erase(pane);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

PaneNotificationState& NotificationVisualState::ensureState(PaneId pane) {
    auto it = states_.find(pane);
    if (it != states_.end()) {
        return it->second;
    }
    auto& s = states_[pane];
    s.pane_id = pane;
    return s;
}

} // namespace termcore
