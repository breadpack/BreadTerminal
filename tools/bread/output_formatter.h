#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace bread {

/// Format a JSON-RPC response for human-readable output.
/// Returns the formatted string. Sets exit_code to 0 on success, 1 on error.
std::string formatResponse(const nlohmann::json& response, bool json_mode, int& exit_code);

}  // namespace bread
