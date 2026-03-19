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

#else // !_WIN32

std::vector<ListeningPort> PortDetector::getAllListeningPorts() {
    // TODO: Implement for Unix using /proc/net/tcp parsing
    return {};
}

std::vector<uint32_t> PortDetector::getProcessTree(uint32_t root_pid) {
    // TODO: Implement for Unix using /proc traversal
    return {root_pid};
}

#endif

} // namespace termcore
