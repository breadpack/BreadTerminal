#include "termcore/ssh_known_hosts.h"
#include "termcore/ssh_transport.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace termcore {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SshKnownHosts::SshKnownHosts(const std::string& path) {
    if (path.empty()) {
        path_ = expandHomePath("~/.ssh/known_hosts");
    } else {
        path_ = path;
    }
}

// ---------------------------------------------------------------------------
// Host key formatting
// ---------------------------------------------------------------------------

std::string SshKnownHosts::makeHostKey(const std::string& hostname, int port) {
    if (port == 22) {
        return hostname;
    }
    return "[" + hostname + "]:" + std::to_string(port);
}

// ---------------------------------------------------------------------------
// Check
// ---------------------------------------------------------------------------

KnownHostResult SshKnownHosts::check(const std::string& hostname, int port,
                                      const std::string& key_type,
                                      const std::string& key_base64) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ifstream file(path_);
    if (!file.is_open()) {
        return KnownHostResult::NotFound;
    }

    std::string target = makeHostKey(hostname, port);
    std::string line;
    bool host_found = false;

    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string file_host, file_type, file_key;
        if (!(iss >> file_host >> file_type >> file_key)) continue;

        if (file_host == target) {
            host_found = true;
            if (file_type == key_type && file_key == key_base64) {
                return KnownHostResult::Match;
            }
        }
    }

    return host_found ? KnownHostResult::Mismatch : KnownHostResult::NotFound;
}

// ---------------------------------------------------------------------------
// Add entry
// ---------------------------------------------------------------------------

bool SshKnownHosts::addEntry(const std::string& hostname, int port,
                              const std::string& key_type,
                              const std::string& key_base64) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Ensure the parent directory exists
    std::filesystem::path p(path_);
    auto parent = p.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    std::ofstream file(path_, std::ios::app);
    if (!file.is_open()) return false;

    std::string host_key = makeHostKey(hostname, port);
    file << host_key << " " << key_type << " " << key_base64 << "\n";
    return file.good();
}

} // namespace termcore
