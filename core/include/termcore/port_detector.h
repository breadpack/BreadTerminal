#ifndef TERMCORE_PORT_DETECTOR_H
#define TERMCORE_PORT_DETECTOR_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

/// Information about a listening TCP port
struct ListeningPort {
    uint16_t port = 0;
    uint32_t pid = 0;
    std::string protocol; // "tcp"
};

/// Detects listening TCP ports associated with a given PID or process tree.
/// Platform-specific: uses GetExtendedTcpTable on Windows.
class PortDetector {
public:
    PortDetector() = default;
    ~PortDetector() = default;

    /// Detect all listening TCP ports owned by the given PID.
    std::vector<ListeningPort> detectPorts(uint32_t pid);

    /// Detect listening TCP ports for the entire process tree rooted at root_pid.
    /// Includes the root process and all descendant processes.
    std::vector<ListeningPort> detectPortsForTree(uint32_t root_pid);

private:
    /// Get all listening TCP ports on the system.
    std::vector<ListeningPort> getAllListeningPorts();

    /// Collect all descendant PIDs of the given root PID (including root itself).
    std::vector<uint32_t> getProcessTree(uint32_t root_pid);
};

} // namespace termcore

#endif // TERMCORE_PORT_DETECTOR_H
