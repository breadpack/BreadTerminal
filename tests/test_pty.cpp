#include <gtest/gtest.h>
#include "termcore/pty.h"

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace termcore;

namespace {

/// Helper: read from PTY with a timeout, accumulating into a string.
/// Returns all data read within the timeout period.
std::string readWithTimeout(Pty& pty, int timeout_ms = 2000) {
    std::string result;
    char buf[4096];
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        int n = pty.read(buf, sizeof(buf));
        if (n > 0) {
            result.append(buf, static_cast<size_t>(n));
        } else if (n < 0) {
            break; // error or closed
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return result;
}

/// Helper: wait until predicate on accumulated output is true, or timeout.
std::string readUntil(Pty& pty, const std::string& needle,
                      int timeout_ms = 3000) {
    std::string result;
    char buf[4096];
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        int n = pty.read(buf, sizeof(buf));
        if (n > 0) {
            result.append(buf, static_cast<size_t>(n));
            if (result.find(needle) != std::string::npos) {
                return result;
            }
        } else if (n < 0) {
            break;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return result;
}

} // namespace

TEST(PtyTest, SpawnDefaultShellIsAlive) {
    auto pty = createPty();
    ASSERT_NE(pty, nullptr);

    ASSERT_TRUE(pty->spawn());

    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(pty->isAlive());
    EXPECT_GT(pty->pid(), 0);
    EXPECT_GE(pty->fd(), 0);
}

TEST(PtyTest, EchoCommand) {
    auto pty = createPty();
    ASSERT_TRUE(pty->spawn("/bin/echo", {"hello_pty_test"}));

    std::string output = readUntil(*pty, "hello_pty_test");
    EXPECT_NE(output.find("hello_pty_test"), std::string::npos);
}

TEST(PtyTest, ResizeNoCrash) {
    auto pty = createPty();
    ASSERT_TRUE(pty->spawn());

    // Resize multiple times — should not crash
    pty->resize(40, 120);
    pty->resize(25, 80);
    pty->resize(50, 200);

    EXPECT_TRUE(pty->isAlive());
}

TEST(PtyTest, CommandExitsWithCode) {
    // "true" exits with 0
    {
        auto pty = createPty();
        ASSERT_TRUE(pty->spawn("/usr/bin/true"));
        int code = pty->waitForExit();
        EXPECT_EQ(code, 0);
        EXPECT_FALSE(pty->isAlive());
    }

    // "false" exits with 1
    {
        auto pty = createPty();
        ASSERT_TRUE(pty->spawn("/usr/bin/false"));
        int code = pty->waitForExit();
        EXPECT_EQ(code, 1);
        EXPECT_FALSE(pty->isAlive());
    }
}

TEST(PtyTest, CatEchoBack) {
    auto pty = createPty();
    ASSERT_TRUE(pty->spawn("/bin/cat"));

    // Give cat time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const char* msg = "pty_echo_test_data";
    pty->write(msg, std::strlen(msg));

    std::string output = readUntil(*pty, "pty_echo_test_data");
    EXPECT_NE(output.find("pty_echo_test_data"), std::string::npos);
}

TEST(PtyTest, NonBlockingReadReturnsZero) {
    auto pty = createPty();
    ASSERT_TRUE(pty->spawn("/bin/cat"));

    // Give it a moment to start, then drain any initial output
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    char drain[4096];
    while (pty->read(drain, sizeof(drain)) > 0) {
        // drain
    }

    // Now read again — should return 0 (no data available)
    char buf[256];
    int n = pty->read(buf, sizeof(buf));
    EXPECT_EQ(n, 0);
}

TEST(PtyTest, FdIsValid) {
    auto pty = createPty();
    ASSERT_TRUE(pty->spawn());

    EXPECT_GE(pty->fd(), 0);
}
