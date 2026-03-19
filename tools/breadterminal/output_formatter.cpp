#include "output_formatter.h"

#include <sstream>

namespace breadterminal {

static std::string formatTable(const nlohmann::json& result) {
    std::ostringstream oss;

    // workspace.list
    if (result.contains("workspaces") && result["workspaces"].is_array()) {
        oss << "ID\tName\tTabs\tActive\n";
        for (const auto& ws : result["workspaces"]) {
            oss << ws.value("id", 0) << "\t"
                << ws.value("name", "") << "\t"
                << ws.value("tab_count", 0) << "\t"
                << (ws.value("active", false) ? "*" : "") << "\n";
        }
        return oss.str();
    }

    // tab.list
    if (result.contains("tabs") && result["tabs"].is_array()) {
        oss << "ID\tTitle\tPanes\tActive Pane\n";
        for (const auto& tab : result["tabs"]) {
            oss << tab.value("id", 0) << "\t"
                << tab.value("title", "") << "\t"
                << tab.value("pane_count", 0) << "\t"
                << tab.value("active_pane_id", 0) << "\n";
        }
        return oss.str();
    }

    // pane.list
    if (result.contains("panes") && result["panes"].is_array()) {
        oss << "ID\tActive\n";
        for (const auto& p : result["panes"]) {
            oss << p.value("id", 0) << "\t"
                << (p.value("is_active", false) ? "*" : "") << "\n";
        }
        return oss.str();
    }

    // agent-state list
    if (result.contains("agents") && result["agents"].is_array()) {
        oss << "Pane\tType\tState\tName\tPID\n";
        for (const auto& a : result["agents"]) {
            oss << a.value("pane_id", 0) << "\t"
                << a.value("type", 0) << "\t"
                << a.value("state", "") << "\t"
                << a.value("name", "") << "\t"
                << a.value("pid", -1) << "\n";
        }
        return oss.str();
    }

    // Simple success
    if (result.contains("success") && result["success"].get<bool>()) {
        return "OK\n";
    }

    // Single-value results
    if (result.contains("workspace_id")) {
        oss << "workspace_id: " << result["workspace_id"] << "\n";
        if (result.contains("name")) {
            oss << "name: " << result["name"].get<std::string>() << "\n";
        }
        if (result.contains("tab_id")) {
            oss << "tab_id: " << result["tab_id"] << "\n";
        }
        if (result.contains("pane_id")) {
            oss << "pane_id: " << result["pane_id"] << "\n";
        }
        return oss.str();
    }

    if (result.contains("tab_id")) {
        oss << "tab_id: " << result["tab_id"] << "\n";
        return oss.str();
    }

    if (result.contains("pane_id") && !result.contains("panes")) {
        oss << "pane_id: " << result["pane_id"] << "\n";
        return oss.str();
    }

    if (result.contains("notification_id")) {
        oss << "notification_id: " << result["notification_id"] << "\n";
        return oss.str();
    }

    if (result.contains("bytes_written")) {
        oss << "bytes_written: " << result["bytes_written"] << "\n";
        return oss.str();
    }

    // Fallback: dump as JSON
    return result.dump(2) + "\n";
}

std::string formatResponse(const nlohmann::json& response, bool json_mode, int& exit_code) {
    exit_code = 0;

    if (json_mode) {
        // Raw JSON mode
        if (response.contains("error")) {
            exit_code = 1;
        }
        return response.dump(2) + "\n";
    }

    if (response.contains("error")) {
        exit_code = 1;
        auto& err = response["error"];
        std::string msg = "Error";
        if (err.contains("code")) {
            msg += " [" + std::to_string(err["code"].get<int>()) + "]";
        }
        if (err.contains("message") && err["message"].is_string()) {
            msg += ": " + err["message"].get<std::string>();
        }
        return msg + "\n";
    }

    if (response.contains("result")) {
        return formatTable(response["result"]);
    }

    return response.dump(2) + "\n";
}

}  // namespace breadterminal
