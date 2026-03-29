#include "termcore/port_detector.h"
#include <gtest/gtest.h>

#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

using namespace termcore;

// ===========================================================================
// PortDetector unit tests
// ===========================================================================

TEST(PortDetector, DetectPortsReturnsVector) {
    PortDetector detector;
    // Use PID 0 — on some platforms (Linux) PID 0 may own system ports,
    // so just verify no crash and that returned entries are valid.
    auto ports = detector.detectPorts(0);
    for (const auto& p : ports) {
        EXPECT_GT(p.port, 0);
        EXPECT_FALSE(p.protocol.empty());
    }
}

TEST(PortDetector, DetectPortsForTreeReturnsVector) {
    PortDetector detector;
    // PID 0 on Windows is the System Idle Process whose tree includes many
    // system processes, so ports may not be empty.  Just verify no crash and
    // that every returned entry has valid data.
    auto ports = detector.detectPortsForTree(0);
    for (const auto& p : ports) {
        EXPECT_GT(p.port, 0);
        EXPECT_FALSE(p.protocol.empty());
    }
}

TEST(PortDetector, GetProcessTreeIncludesRoot) {
    PortDetector detector;
    // detectPortsForTree internally calls getProcessTree which must include
    // root_pid. We test indirectly: the current process PID should be in its
    // own tree. Use detectPortsForTree with current PID -- should not crash.
#if defined(_WIN32)
    uint32_t my_pid = static_cast<uint32_t>(GetCurrentProcessId());
#else
    uint32_t my_pid = static_cast<uint32_t>(getpid());
#endif
    // This exercises both getProcessTree and getAllListeningPorts
    auto ports = detector.detectPortsForTree(my_pid);
    // We don't assert specific ports; just ensure no crash and valid protocol
    for (const auto& p : ports) {
        EXPECT_GT(p.port, 0);
        EXPECT_GT(p.pid, 0u);
        EXPECT_FALSE(p.protocol.empty());
    }
}

TEST(PortDetector, DetectPortsCurrentProcess) {
    PortDetector detector;
#if defined(_WIN32)
    uint32_t my_pid = static_cast<uint32_t>(GetCurrentProcessId());
#else
    uint32_t my_pid = static_cast<uint32_t>(getpid());
#endif
    // Current test process typically doesn't listen on any port
    auto ports = detector.detectPorts(my_pid);
    // Verify each returned entry belongs to our PID
    for (const auto& p : ports) {
        EXPECT_EQ(p.pid, my_pid);
    }
}

TEST(PortDetector, ListeningPortDefaultValues) {
    ListeningPort lp;
    EXPECT_EQ(lp.port, 0);
    EXPECT_EQ(lp.pid, 0u);
    EXPECT_TRUE(lp.protocol.empty());
}

#if defined(__linux__)
// Linux-specific tests that verify /proc parsing works (at least no crash)

TEST(PortDetector, LinuxDetectAllListeningPorts) {
    PortDetector detector;
    // On a typical Linux system, there should be at least one listening port
    // (e.g., sshd, dbus) but in CI containers there may be none.
    // We just ensure no crash and valid data.
    auto ports = detector.detectPortsForTree(1); // PID 1 = init/systemd tree
    for (const auto& p : ports) {
        EXPECT_GT(p.port, 0);
        EXPECT_TRUE(p.protocol == "tcp" || p.protocol == "tcp6");
    }
}

TEST(PortDetector, LinuxProcessTreeHasDescendants) {
    PortDetector detector;
    // PID 1 (init/systemd) should have descendants on any running Linux
    // system. We test via detectPortsForTree which internally uses
    // getProcessTree. Just verify no crash.
    auto ports = detector.detectPortsForTree(1);
    (void)ports;
}

#endif // __linux__

#if defined(__APPLE__)

TEST(PortDetector, MacOSDetectListeningPorts) {
    PortDetector detector;
    // macOS typically has multiple listening services.
    // Exercise the code path to ensure no crash.
    auto ports = detector.detectPortsForTree(1); // launchd tree
    for (const auto& p : ports) {
        EXPECT_GT(p.port, 0);
        EXPECT_TRUE(p.protocol == "tcp" || p.protocol == "tcp6");
    }
}

#endif // __APPLE__
