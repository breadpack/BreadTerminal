#pragma once

#include "termcore/agent.h"
#include "termcore/agent_tree_tracker.h"
#include "termcore/notification.h"

#include <nlohmann/json.hpp>
#include <string>

namespace termcore {

class HookBridge {
public:
    HookBridge(AgentTreeTracker& tree, AgentTracker& tracker,
               NotificationStore& notifications);

    void processHookEvent(const nlohmann::json& event);

private:
    void handleSubagentStart(const nlohmann::json& event);
    void handleSubagentStop(const nlohmann::json& event);
    void handleNotification(const nlohmann::json& event);
    void handleStateChange(const nlohmann::json& event);

    AgentTreeTracker& tree_;
    AgentTracker& tracker_;
    NotificationStore& notifications_;
};

} // namespace termcore
