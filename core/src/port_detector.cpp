#include "termcore/port_detector.h"

#include <algorithm>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <tlhelp32.h>
#pragma comment(lib, "iphlpapi.lib")
#elif defined(__linux__)
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#elif defined(__APPLE__)
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <libproc.h>
#include <sys/sysctl.h>
#endif

namespace termcore {

std::vector<ListeningPort> PortDetector::detectPorts(uint32_t pid) {
    auto all = getAllListeningPorts();
    std::vector<ListeningPort> result;
    for (auto& entry : all) {
        if (entry.pid == pid) {
            result.push_back(std::move(entry));
        }
    }
    return result;
}

std::vector<ListeningPort> PortDetector::detectPortsForTree(uint32_t root_pid) {
    auto tree_pids = getProcessTree(root_pid);
    std::unordered_set<uint32_t> pid_set(tree_pids.begin(), tree_pids.end());

    auto all = getAllListeningPorts();
    std::vector<ListeningPort> result;
    for (auto& entry : all) {
        if (pid_set.count(entry.pid)) {
            result.push_back(std::move(entry));
        }
    }
    return result;
}

#if defined(_WIN32)

std::vector<ListeningPort> PortDetector::getAllListeningPorts() {
    std::vector<ListeningPort> result;

    // Query the TCP table with PID information, listeners only
    ULONG size = 0;
    DWORD ret = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                                     TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER) {
        return result;
    }

    std::vector<uint8_t> buffer(size);
    ret = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET,
                               TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (ret != NO_ERROR) {
        return result;
    }

    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        // Only include entries in LISTEN state
        if (row.dwState == MIB_TCP_STATE_LISTEN) {
            ListeningPort lp;
            lp.port = ntohs(static_cast<uint16_t>(row.dwLocalPort));
            lp.pid = row.dwOwningPid;
            lp.protocol = "tcp";
            result.push_back(std::move(lp));
        }
    }

    return result;
}

std::vector<uint32_t> PortDetector::getProcessTree(uint32_t root_pid) {
    std::vector<uint32_t> tree;
    tree.push_back(root_pid);

    // Take a snapshot of all processes
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return tree;
    }

    // Build parent->children map
    struct ProcessEntry {
        DWORD pid;
        DWORD parent_pid;
    };
    std::vector<ProcessEntry> entries;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snapshot, &pe)) {
        do {
            entries.push_back({pe.th32ProcessID, pe.th32ParentProcessID});
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);

    // BFS to find all descendants
    std::unordered_set<uint32_t> visited;
    visited.insert(root_pid);

    size_t front = 0;
    while (front < tree.size()) {
        uint32_t current = tree[front++];
        for (const auto& e : entries) {
            if (e.parent_pid == current && visited.find(e.pid) == visited.end()) {
                visited.insert(e.pid);
                tree.push_back(e.pid);
            }
        }
    }

    return tree;
}

#elif defined(__linux__)

// ---------------------------------------------------------------------------
// Linux: parse /proc/net/tcp and /proc/net/tcp6, resolve inode -> PID
// ---------------------------------------------------------------------------

namespace {

// Parse a hex port from /proc/net/tcp local_address field (e.g. "0100007F:1F90")
// Returns the port number, or 0 on failure.
uint16_t parseHexPort(const std::string& addr_field) {
    auto colon = addr_field.find(':');
    if (colon == std::string::npos) return 0;
    unsigned long port = 0;
    try {
        port = std::stoul(addr_field.substr(colon + 1), nullptr, 16);
    } catch (...) {
        return 0;
    }
    return static_cast<uint16_t>(port);
}

// Build a map from socket inode -> PID by scanning /proc/[pid]/fd/ symlinks.
std::unordered_map<uint64_t, uint32_t> buildInodeToPidMap() {
    std::unordered_map<uint64_t, uint32_t> inode_to_pid;

    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) return inode_to_pid;

    struct dirent* proc_entry;
    while ((proc_entry = readdir(proc_dir)) != nullptr) {
        // Only numeric directory names (PIDs)
        if (proc_entry->d_type != DT_DIR) continue;
        char* end = nullptr;
        unsigned long pid = strtoul(proc_entry->d_name, &end, 10);
        if (end == proc_entry->d_name || *end != '\0') continue;

        std::string fd_path = std::string("/proc/") + proc_entry->d_name + "/fd";
        DIR* fd_dir = opendir(fd_path.c_str());
        if (!fd_dir) continue; // permission denied is common

        struct dirent* fd_entry;
        while ((fd_entry = readdir(fd_dir)) != nullptr) {
            std::string link_path = fd_path + "/" + fd_entry->d_name;
            char link_target[256];
            ssize_t len = readlink(link_path.c_str(), link_target,
                                   sizeof(link_target) - 1);
            if (len <= 0) continue;
            link_target[len] = '\0';

            // Match "socket:[inode]"
            if (strncmp(link_target, "socket:[", 8) == 0) {
                char* inode_end = nullptr;
                uint64_t inode = strtoull(link_target + 8, &inode_end, 10);
                if (inode_end && *inode_end == ']') {
                    inode_to_pid[inode] = static_cast<uint32_t>(pid);
                }
            }
        }
        closedir(fd_dir);
    }
    closedir(proc_dir);

    return inode_to_pid;
}

// Parse /proc/net/tcp or /proc/net/tcp6 for listening sockets.
// Each line (after header) has fields:
//   sl local_address rem_address st tx_queue:rx_queue tr:tm->when retrnsmt
//   uid timeout inode ...
// State 0A = TCP_LISTEN.
void parseProcNetTcp(
    const std::string& path,
    const std::string& protocol,
    const std::unordered_map<uint64_t, uint32_t>& inode_to_pid,
    std::vector<ListeningPort>& result) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    // Skip header line
    if (!std::getline(file, line)) return;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string sl, local_addr, rem_addr, state_hex;
        if (!(iss >> sl >> local_addr >> rem_addr >> state_hex)) continue;

        // Only LISTEN state (0A)
        if (state_hex != "0A") continue;

        uint16_t port = parseHexPort(local_addr);
        if (port == 0) continue;

        // Read remaining fields to reach inode (field index 9, 0-based)
        // After state: tx_queue:rx_queue, tr:tm_when, retrnsmt, uid,
        // timeout, inode
        std::string tx_rx, tr_tm, retrnsmt, uid_str, timeout_str, inode_str;
        if (!(iss >> tx_rx >> tr_tm >> retrnsmt >> uid_str >> timeout_str
              >> inode_str))
            continue;

        uint64_t inode = 0;
        try {
            inode = std::stoull(inode_str);
        } catch (...) {
            continue;
        }

        uint32_t pid = 0;
        auto it = inode_to_pid.find(inode);
        if (it != inode_to_pid.end()) {
            pid = it->second;
        }

        ListeningPort lp;
        lp.port = port;
        lp.pid = pid;
        lp.protocol = protocol;
        result.push_back(std::move(lp));
    }
}

} // anonymous namespace

std::vector<ListeningPort> PortDetector::getAllListeningPorts() {
    std::vector<ListeningPort> result;

    auto inode_to_pid = buildInodeToPidMap();
    parseProcNetTcp("/proc/net/tcp", "tcp", inode_to_pid, result);
    parseProcNetTcp("/proc/net/tcp6", "tcp6", inode_to_pid, result);

    return result;
}

std::vector<uint32_t> PortDetector::getProcessTree(uint32_t root_pid) {
    std::vector<uint32_t> tree;
    tree.push_back(root_pid);

    // Build a map of pid -> parent_pid by reading /proc/[pid]/status
    struct ProcessEntry {
        uint32_t pid;
        uint32_t parent_pid;
    };
    std::vector<ProcessEntry> entries;

    DIR* proc_dir = opendir("/proc");
    if (!proc_dir) return tree;

    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        char* end = nullptr;
        unsigned long pid = strtoul(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0') continue;

        std::string status_path =
            std::string("/proc/") + entry->d_name + "/status";
        std::ifstream status_file(status_path);
        if (!status_file.is_open()) continue;

        std::string line;
        uint32_t ppid = 0;
        while (std::getline(status_file, line)) {
            if (line.compare(0, 5, "PPid:") == 0) {
                try {
                    ppid = static_cast<uint32_t>(
                        std::stoul(line.substr(5)));
                } catch (...) {
                    ppid = 0;
                }
                break;
            }
        }

        entries.push_back({static_cast<uint32_t>(pid), ppid});
    }
    closedir(proc_dir);

    // BFS to find all descendants
    std::unordered_set<uint32_t> visited;
    visited.insert(root_pid);

    size_t front = 0;
    while (front < tree.size()) {
        uint32_t current = tree[front++];
        for (const auto& e : entries) {
            if (e.parent_pid == current
                && visited.find(e.pid) == visited.end()) {
                visited.insert(e.pid);
                tree.push_back(e.pid);
            }
        }
    }

    return tree;
}

#elif defined(__APPLE__)

// ---------------------------------------------------------------------------
// macOS: use libproc to enumerate listening sockets and process hierarchy
// ---------------------------------------------------------------------------

namespace {

// Get parent PID using sysctl
uint32_t getParentPid(uint32_t pid) {
    struct kinfo_proc info;
    memset(&info, 0, sizeof(info));
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID,
                   static_cast<int>(pid)};
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0 && size > 0) {
        return static_cast<uint32_t>(info.kp_eproc.e_ppid);
    }
    return 0;
}

// Get all PIDs on the system
std::vector<uint32_t> getAllPids() {
    std::vector<uint32_t> pids;
    int count = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (count <= 0) return pids;

    std::vector<pid_t> buffer(count);
    int actual = proc_listpids(PROC_ALL_PIDS, 0, buffer.data(),
                               static_cast<int>(buffer.size()
                                                 * sizeof(pid_t)));
    if (actual <= 0) return pids;

    int num_pids = actual / static_cast<int>(sizeof(pid_t));
    for (int i = 0; i < num_pids; ++i) {
        if (buffer[i] > 0) {
            pids.push_back(static_cast<uint32_t>(buffer[i]));
        }
    }
    return pids;
}

} // anonymous namespace

std::vector<ListeningPort> PortDetector::getAllListeningPorts() {
    std::vector<ListeningPort> result;

    auto pids = getAllPids();
    for (uint32_t pid : pids) {
        // Get the number of file descriptors for this process
        int buf_size = proc_pidinfo(static_cast<int>(pid),
                                     PROC_PIDLISTFDS, 0, nullptr, 0);
        if (buf_size <= 0) continue;

        std::vector<char> buf(buf_size);
        int actual_size = proc_pidinfo(static_cast<int>(pid),
                                        PROC_PIDLISTFDS, 0,
                                        buf.data(), buf_size);
        if (actual_size <= 0) continue;

        int num_fds = actual_size
                      / static_cast<int>(sizeof(struct proc_fdinfo));
        auto* fd_info =
            reinterpret_cast<struct proc_fdinfo*>(buf.data());

        for (int i = 0; i < num_fds; ++i) {
            if (fd_info[i].proc_fdtype != PROX_FDTYPE_SOCKET) continue;

            struct socket_fdinfo socket_info;
            int si_size = proc_pidfdinfo(
                static_cast<int>(pid), fd_info[i].proc_fd,
                PROC_PIDFDSOCKETINFO, &socket_info,
                sizeof(socket_info));
            if (si_size != sizeof(socket_info)) continue;

            // Only TCP sockets in LISTEN state
            auto& si = socket_info.psi;
            if (si.soi_kind != SOCKINFO_TCP) continue;
            if (si.soi_proto.pri_tcp.tcpsi_state != TSI_S_LISTEN)
                continue;

            uint16_t port = 0;
            std::string protocol;

            if (si.soi_family == AF_INET) {
                port = ntohs(
                    si.soi_proto.pri_tcp.tcpsi_ini.insi_lport);
                protocol = "tcp";
            } else if (si.soi_family == AF_INET6) {
                port = ntohs(
                    si.soi_proto.pri_tcp.tcpsi_ini.insi_lport);
                protocol = "tcp6";
            } else {
                continue;
            }

            if (port == 0) continue;

            ListeningPort lp;
            lp.port = port;
            lp.pid = pid;
            lp.protocol = protocol;
            result.push_back(std::move(lp));
        }
    }

    return result;
}

std::vector<uint32_t> PortDetector::getProcessTree(uint32_t root_pid) {
    std::vector<uint32_t> tree;
    tree.push_back(root_pid);

    // Build pid -> parent_pid map
    struct ProcessEntry {
        uint32_t pid;
        uint32_t parent_pid;
    };
    std::vector<ProcessEntry> entries;

    auto pids = getAllPids();
    for (uint32_t pid : pids) {
        uint32_t ppid = getParentPid(pid);
        entries.push_back({pid, ppid});
    }

    // BFS to find all descendants
    std::unordered_set<uint32_t> visited;
    visited.insert(root_pid);

    size_t front = 0;
    while (front < tree.size()) {
        uint32_t current = tree[front++];
        for (const auto& e : entries) {
            if (e.parent_pid == current
                && visited.find(e.pid) == visited.end()) {
                visited.insert(e.pid);
                tree.push_back(e.pid);
            }
        }
    }

    return tree;
}

#else

// Unsupported platform: return empty results gracefully
std::vector<ListeningPort> PortDetector::getAllListeningPorts() {
    return {};
}

std::vector<uint32_t> PortDetector::getProcessTree(uint32_t root_pid) {
    return {root_pid};
}

#endif

} // namespace termcore
