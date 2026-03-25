#include "termcore/hook_bridge.h"

namespace termcore {

HookBridge::HookBridge(AgentTreeTracker& tree, AgentTracker& tracker,
                       NotificationStore& notifications)
    : tree_(tree), tracker_(tracker), notifications_(notifications) {}

void HookBridge::processHookEvent(const nlohmann::json& event) {
    auto it = event.find("event");
    if (it == event.end() || !it->is_string()) return;

    const std::string& type = it->get_ref<const std::string&>();
    if (type == "SubagentStart")       handleSubagentStart(event);
    else if (type == "SubagentStop")   handleSubagentStop(event);
    else if (type == "Notification")   handleNotification(event);
    else if (type == "StateChange")    handleStateChange(event);
    // Unknown events silently ignored
}

void HookBridge::handleSubagentStart(const nlohmann::json& e) {
    auto id = e.value("agent_id", "");
    if (id.empty()) return;
    auto pane = e.value("pane_id", 0u);
    auto type = e.value("agent_type", "Unknown");
    auto desc = e.value("description", "");
    auto parent = e.value("parent_agent_id", "");
    tree_.onAgentStart(pane, id, type, desc, parent);
}

void HookBridge::handleSubagentStop(const nlohmann::json& e) {
    auto id = e.value("agent_id", "");
    if (id.empty()) return;
    auto pane = e.value("pane_id", 0u);
    tree_.onAgentStop(pane, id, AgentState::Exited);
}

void HookBridge::handleNotification(const nlohmann::json& e) {
    auto pane = e.value("pane_id", 0u);
    auto title = e.value("title", "");
    auto body = e.value("body", "");
    auto urgency_str = e.value("urgency", "normal");
    auto urgency = (urgency_str == "critical") ? NotificationUrgency::Critical
                 : (urgency_str == "low")      ? NotificationUrgency::Low
                                               : NotificationUrgency::Normal;
    notifications_.add(pane, NotificationSource::Agent, urgency, title, body);
}

void HookBridge::handleStateChange(const nlohmann::json& e) {
    auto id = e.value("agent_id", "");
    if (id.empty()) return;
    auto pane = e.value("pane_id", 0u);
    auto state_str = e.value("state", "");
    auto state = AgentTracker::stringToState(state_str);
    tree_.onAgentStateChange(pane, id, state);
}

} // namespace termcore
