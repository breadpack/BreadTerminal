// Unit tests for InputRingBuffer (lock-free SPSC ring buffer).
//
// The InputRingBuffer and InputEvent structs live in the Windows-specific
// header TerminalWindowState.h, but the algorithm is platform-independent.
// We duplicate the minimal definitions here to avoid pulling in D3D/Win32
// headers, keeping the test buildable on all platforms.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal copies of InputEvent / InputRingBuffer (must stay in sync with
// platform/windows/include/TerminalWindowState.h).
// ---------------------------------------------------------------------------

namespace ring_buffer_test {

using WPARAM = uintptr_t;  // platform-neutral stand-in

struct InputEvent {
    enum Type : uint8_t { KeyDown, Char };
    Type type;
    WPARAM wParam;
    uint8_t mods;
};

struct InputRingBuffer {
    static constexpr size_t kCapacity = 256;  // must be power of 2

    bool push(const InputEvent& ev) {
        size_t w = write_.load(std::memory_order_relaxed);
        size_t next = (w + 1) & (kCapacity - 1);
        if (next == read_.load(std::memory_order_acquire)) return false; // full
        buf_[w] = ev;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(InputEvent& ev) {
        size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false; // empty
        ev = buf_[r];
        read_.store((r + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return read_.load(std::memory_order_acquire) == write_.load(std::memory_order_acquire);
    }

private:
    std::array<InputEvent, kCapacity> buf_{};
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};
};

}  // namespace ring_buffer_test

using ring_buffer_test::InputEvent;
using ring_buffer_test::InputRingBuffer;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
static InputEvent makeEvent(InputEvent::Type type, uintptr_t key, uint8_t mods = 0) {
    return InputEvent{type, key, mods};
}

// ===========================================================================
// Test cases
// ===========================================================================

TEST(InputRingBuffer, InitiallyEmpty) {
    InputRingBuffer rb;
    EXPECT_TRUE(rb.empty());

    InputEvent ev{};
    EXPECT_FALSE(rb.pop(ev));
}

TEST(InputRingBuffer, PushPopSingle) {
    InputRingBuffer rb;

    auto ev = makeEvent(InputEvent::KeyDown, 'A', 0x01);
    EXPECT_TRUE(rb.push(ev));
    EXPECT_FALSE(rb.empty());

    InputEvent out{};
    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.type, InputEvent::KeyDown);
    EXPECT_EQ(out.wParam, static_cast<uintptr_t>('A'));
    EXPECT_EQ(out.mods, 0x01);

    EXPECT_TRUE(rb.empty());
}

TEST(InputRingBuffer, FIFOOrdering) {
    InputRingBuffer rb;

    constexpr int N = 10;
    for (int i = 0; i < N; ++i) {
        EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, 'A' + i, static_cast<uint8_t>(i))));
    }

    for (int i = 0; i < N; ++i) {
        InputEvent out{};
        EXPECT_TRUE(rb.pop(out));
        EXPECT_EQ(out.wParam, static_cast<uintptr_t>('A' + i));
        EXPECT_EQ(out.mods, static_cast<uint8_t>(i));
    }

    EXPECT_TRUE(rb.empty());
}

TEST(InputRingBuffer, CapacityBoundary) {
    InputRingBuffer rb;

    // Usable capacity is kCapacity - 1 (one slot is sentinel for full detection).
    constexpr size_t usable = InputRingBuffer::kCapacity - 1;

    for (size_t i = 0; i < usable; ++i) {
        EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, i)));
    }

    // Buffer is now full — next push should fail.
    EXPECT_FALSE(rb.push(makeEvent(InputEvent::KeyDown, 999)));
    EXPECT_FALSE(rb.empty());

    // Drain all and verify ordering.
    for (size_t i = 0; i < usable; ++i) {
        InputEvent out{};
        EXPECT_TRUE(rb.pop(out));
        EXPECT_EQ(out.wParam, i);
    }

    EXPECT_TRUE(rb.empty());
}

TEST(InputRingBuffer, PopOnEmptyReturnsFalse) {
    InputRingBuffer rb;

    InputEvent out{};
    EXPECT_FALSE(rb.pop(out));

    // Push one, pop it, then pop again on empty.
    EXPECT_TRUE(rb.push(makeEvent(InputEvent::Char, 'x')));
    EXPECT_TRUE(rb.pop(out));
    EXPECT_FALSE(rb.pop(out));
}

TEST(InputRingBuffer, WrapAround) {
    InputRingBuffer rb;

    constexpr size_t usable = InputRingBuffer::kCapacity - 1;

    // Fill and drain twice to force the internal indices to wrap around.
    for (int round = 0; round < 3; ++round) {
        for (size_t i = 0; i < usable; ++i) {
            EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, round * 1000 + i)));
        }
        // Full.
        EXPECT_FALSE(rb.push(makeEvent(InputEvent::KeyDown, 0xDEAD)));

        for (size_t i = 0; i < usable; ++i) {
            InputEvent out{};
            EXPECT_TRUE(rb.pop(out));
            EXPECT_EQ(out.wParam, static_cast<uintptr_t>(round * 1000 + i));
        }
        EXPECT_TRUE(rb.empty());
    }
}

TEST(InputRingBuffer, MixedPushPop) {
    InputRingBuffer rb;

    // Interleave pushes and pops.
    EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, 1)));
    EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, 2)));

    InputEvent out{};
    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.wParam, 1u);

    EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, 3)));

    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.wParam, 2u);

    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.wParam, 3u);

    EXPECT_TRUE(rb.empty());
}

TEST(InputRingBuffer, KeyEventTypes) {
    InputRingBuffer rb;

    EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, 0x41)));  // 'A'
    EXPECT_TRUE(rb.push(makeEvent(InputEvent::Char, 0x61)));     // 'a'

    InputEvent out{};
    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.type, InputEvent::KeyDown);
    EXPECT_EQ(out.wParam, 0x41u);

    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.type, InputEvent::Char);
    EXPECT_EQ(out.wParam, 0x61u);
}

TEST(InputRingBuffer, OverflowDoesNotCorrupt) {
    InputRingBuffer rb;

    constexpr size_t usable = InputRingBuffer::kCapacity - 1;

    // Fill the buffer.
    for (size_t i = 0; i < usable; ++i) {
        EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, i)));
    }

    // Attempt to push more — should fail silently.
    for (int i = 0; i < 10; ++i) {
        EXPECT_FALSE(rb.push(makeEvent(InputEvent::KeyDown, 0xBAD)));
    }

    // Verify original data is intact.
    for (size_t i = 0; i < usable; ++i) {
        InputEvent out{};
        EXPECT_TRUE(rb.pop(out));
        EXPECT_EQ(out.wParam, i);
    }

    EXPECT_TRUE(rb.empty());
}

TEST(InputRingBuffer, StressPattern) {
    InputRingBuffer rb;

    // Simulate burst input: push a small batch, drain, repeat many times.
    constexpr int iterations = 1000;
    constexpr int batchSize = 8;
    uintptr_t counter = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        // Push batch.
        for (int i = 0; i < batchSize; ++i) {
            EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, counter++)));
        }

        // Drain batch.
        uintptr_t expected = counter - batchSize;
        for (int i = 0; i < batchSize; ++i) {
            InputEvent out{};
            EXPECT_TRUE(rb.pop(out));
            EXPECT_EQ(out.wParam, expected++);
        }

        EXPECT_TRUE(rb.empty());
    }
}

TEST(InputRingBuffer, EmptyAfterExactDrain) {
    InputRingBuffer rb;

    // Push exactly 1 less than capacity, drain all, verify empty.
    constexpr size_t usable = InputRingBuffer::kCapacity - 1;
    for (size_t i = 0; i < usable; ++i) {
        rb.push(makeEvent(InputEvent::Char, i));
    }
    for (size_t i = 0; i < usable; ++i) {
        InputEvent out{};
        rb.pop(out);
    }
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.pop(*(InputEvent*)alloca(sizeof(InputEvent))));
}

TEST(InputRingBuffer, PartialDrainAndRefill) {
    InputRingBuffer rb;

    // Push 100, pop 50, push 100 more, verify all 150 come out in order.
    for (uintptr_t i = 0; i < 100; ++i) {
        EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, i)));
    }

    for (uintptr_t i = 0; i < 50; ++i) {
        InputEvent out{};
        EXPECT_TRUE(rb.pop(out));
        EXPECT_EQ(out.wParam, i);
    }

    for (uintptr_t i = 100; i < 200; ++i) {
        EXPECT_TRUE(rb.push(makeEvent(InputEvent::KeyDown, i)));
    }

    // Pop remaining 50 from first batch + 100 from second batch.
    for (uintptr_t i = 50; i < 200; ++i) {
        InputEvent out{};
        EXPECT_TRUE(rb.pop(out));
        EXPECT_EQ(out.wParam, i);
    }

    EXPECT_TRUE(rb.empty());
}

TEST(InputRingBuffer, ModifierPreservation) {
    InputRingBuffer rb;

    // Verify all fields are preserved through the buffer.
    InputEvent ev;
    ev.type = InputEvent::KeyDown;
    ev.wParam = 0x43;  // 'C'
    ev.mods = 0xFF;    // all modifier bits set

    EXPECT_TRUE(rb.push(ev));

    InputEvent out{};
    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.type, ev.type);
    EXPECT_EQ(out.wParam, ev.wParam);
    EXPECT_EQ(out.mods, ev.mods);
}
