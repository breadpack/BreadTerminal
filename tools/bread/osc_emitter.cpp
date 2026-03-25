#include "osc_emitter.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace bread {

std::string buildOscSequence(const std::string& json_payload) {
    return "\033]7770;" + json_payload + "\033\\";
}

int emitOsc(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Usage: bread osc emit <type> [options]\n"
                  << "Types: state-change, notify, subagent-start, subagent-stop, raw\n";
        return 1;
    }

    std::string type = argv[0];
    nlohmann::json event;

    auto getOpt = [&](const std::string& flag) -> std::string {
        for (int i = 1; i < argc - 1; ++i) {
            if (std::string(argv[i]) == flag)
                return argv[i + 1];
        }
        return "";
    };

    if (type == "state-change") {
        event["event"] = "StateChange";
        auto state = getOpt("--state");
        if (state.empty()) { std::cerr << "Error: --state required\n"; return 1; }
        event["state"] = state;
        auto aid = getOpt("--agent-id");
        if (!aid.empty()) event["agent_id"] = aid;
    } else if (type == "notify") {
        event["event"] = "Notification";
        auto title = getOpt("--title");
        auto body = getOpt("--body");
        if (title.empty() && body.empty()) { std::cerr << "Error: --title or --body required\n"; return 1; }
        if (!title.empty()) event["title"] = title;
        if (!body.empty()) event["body"] = body;
        auto urgency = getOpt("--urgency");
        if (!urgency.empty()) event["urgency"] = urgency;
    } else if (type == "subagent-start") {
        event["event"] = "SubagentStart";
        auto aid = getOpt("--agent-id");
        if (aid.empty()) { std::cerr << "Error: --agent-id required\n"; return 1; }
        event["agent_id"] = aid;
        auto at = getOpt("--agent-type");
        if (!at.empty()) event["agent_type"] = at;
        auto desc = getOpt("--description");
        if (!desc.empty()) event["description"] = desc;
    } else if (type == "subagent-stop") {
        event["event"] = "SubagentStop";
        auto aid = getOpt("--agent-id");
        if (aid.empty()) { std::cerr << "Error: --agent-id required\n"; return 1; }
        event["agent_id"] = aid;
    } else if (type == "raw") {
        if (argc < 2) { std::cerr << "Error: raw JSON argument required\n"; return 1; }
        auto parsed = nlohmann::json::parse(argv[1], nullptr, false);
        if (parsed.is_discarded()) { std::cerr << "Error: invalid JSON\n"; return 1; }
        event = parsed;
    } else {
        std::cerr << "Unknown osc emit type: " << type << "\n";
        return 1;
    }

    std::cout << buildOscSequence(event.dump());
    std::cout.flush();
    return 0;
}

}  // namespace bread
