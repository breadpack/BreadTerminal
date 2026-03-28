// Unit tests for termcore::RingBuffer (circular byte buffer extracted from
// pty_windows.cpp).  Uses a small capacity (64 bytes) to make wraparound
// easy to exercise without large allocations.

#include <gtest/gtest.h>
#include "termcore/ring_buffer.h"

#include <cstring>
#include <numeric>
#include <vector>

namespace {

// Use a small capacity so wraparound tests are straightforward.
using SmallRing = termcore::RingBuffer<64>;

// Also verify the default (128 KB) template instantiation compiles.
using DefaultRing = termcore::RingBuffer<>;

// -----------------------------------------------------------------
// EmptyBufferHasZeroAvailable
// -----------------------------------------------------------------
TEST(RingBuffer, EmptyBufferHasZeroAvailable) {
    SmallRing ring;
    EXPECT_EQ(ring.readAvailable(), 0u);

    // Reading from empty buffer returns 0.
    char tmp[16];
    EXPECT_EQ(ring.read(tmp, sizeof(tmp)), 0u);
}

// -----------------------------------------------------------------
// WriteAndReadBack
// -----------------------------------------------------------------
TEST(RingBuffer, WriteAndReadBack) {
    SmallRing ring;
    const char* msg = "hello";
    size_t len = std::strlen(msg);

    EXPECT_EQ(ring.write(msg, len), len);
    EXPECT_EQ(ring.readAvailable(), len);

    char buf[16] = {};
    EXPECT_EQ(ring.read(buf, sizeof(buf)), len);
    EXPECT_EQ(std::string(buf, len), "hello");

    // Buffer should be empty again.
    EXPECT_EQ(ring.readAvailable(), 0u);
}

// -----------------------------------------------------------------
// WrapAround - Write past capacity, verify wraparound works
// -----------------------------------------------------------------
TEST(RingBuffer, WrapAround) {
    SmallRing ring;
    constexpr size_t cap = SmallRing::kCapacity; // 64

    // Fill the buffer with 'A's (60 bytes, leaving 4 free).
    std::vector<char> fillA(60, 'A');
    EXPECT_EQ(ring.write(fillA.data(), fillA.size()), 60u);

    // Drain 50 bytes to advance the read pointer.
    char drain[60];
    EXPECT_EQ(ring.read(drain, 50), 50u);
    // Now: readAvailable = 10, read_pos=50, write_pos=60

    // Write 50 bytes of 'B' -- this must wrap around the end of the
    // internal buffer (write_pos goes from 60 to 110, wrapping inside
    // the 64-byte array).
    std::vector<char> fillB(50, 'B');
    // Available space = cap - readAvailable() = 64 - 10 = 54, so 50 fits.
    EXPECT_EQ(ring.write(fillB.data(), fillB.size()), 50u);
    EXPECT_EQ(ring.readAvailable(), 60u); // 10 remaining A's + 50 B's

    // Read everything out and verify content.
    char out[64];
    EXPECT_EQ(ring.read(out, sizeof(out)), 60u);

    // First 10 should be 'A', next 50 should be 'B'.
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(out[i], 'A') << "index " << i;
    }
    for (int i = 10; i < 60; ++i) {
        EXPECT_EQ(out[i], 'B') << "index " << i;
    }
}

// -----------------------------------------------------------------
// FullBufferBehavior - Write when full
// -----------------------------------------------------------------
TEST(RingBuffer, FullBufferBehavior) {
    SmallRing ring;
    constexpr size_t cap = SmallRing::kCapacity;

    // Fill to capacity.
    std::vector<char> data(cap, 'X');
    EXPECT_EQ(ring.write(data.data(), data.size()), cap);
    EXPECT_EQ(ring.readAvailable(), cap);

    // Further writes should return 0 (no space).
    char extra = 'Y';
    EXPECT_EQ(ring.write(&extra, 1), 0u);

    // readAvailable unchanged.
    EXPECT_EQ(ring.readAvailable(), cap);

    // After draining 1 byte, we can write 1 byte.
    char tmp;
    EXPECT_EQ(ring.read(&tmp, 1), 1u);
    EXPECT_EQ(tmp, 'X');
    EXPECT_EQ(ring.write(&extra, 1), 1u);
    EXPECT_EQ(ring.readAvailable(), cap);
}

// -----------------------------------------------------------------
// PartialReadWrite - Mixed operations
// -----------------------------------------------------------------
TEST(RingBuffer, PartialReadWrite) {
    SmallRing ring;

    // Interleave writes and reads.
    const char* w1 = "abcdefgh";  // 8 bytes
    const char* w2 = "ijklmnop";  // 8 bytes

    EXPECT_EQ(ring.write(w1, 8), 8u);
    EXPECT_EQ(ring.readAvailable(), 8u);

    // Read 4 bytes.
    char buf[16] = {};
    EXPECT_EQ(ring.read(buf, 4), 4u);
    EXPECT_EQ(std::string(buf, 4), "abcd");
    EXPECT_EQ(ring.readAvailable(), 4u);

    // Write 8 more bytes.
    EXPECT_EQ(ring.write(w2, 8), 8u);
    EXPECT_EQ(ring.readAvailable(), 12u);

    // Read all 12 remaining bytes.
    char out[16] = {};
    EXPECT_EQ(ring.read(out, 16), 12u);
    EXPECT_EQ(std::string(out, 12), "efghijklmnop");
    EXPECT_EQ(ring.readAvailable(), 0u);
}

// -----------------------------------------------------------------
// LargeDataTransfer - Write/read larger than capacity in chunks
// -----------------------------------------------------------------
TEST(RingBuffer, LargeDataTransfer) {
    SmallRing ring;
    constexpr size_t cap = SmallRing::kCapacity; // 64

    // Prepare 1024 bytes of sequential data.
    std::vector<char> source(1024);
    std::iota(source.begin(), source.end(), static_cast<char>(0));

    std::vector<char> sink;
    sink.reserve(1024);

    size_t written_total = 0;
    size_t read_total = 0;

    while (written_total < source.size()) {
        // Write as much as the buffer will accept.
        size_t to_write = source.size() - written_total;
        size_t w = ring.write(source.data() + written_total, to_write);
        written_total += w;

        // Read everything available.
        char tmp[128];
        size_t r = ring.read(tmp, sizeof(tmp));
        sink.insert(sink.end(), tmp, tmp + r);
        read_total += r;
    }

    // Drain any remaining data.
    while (ring.readAvailable() > 0) {
        char tmp[128];
        size_t r = ring.read(tmp, sizeof(tmp));
        sink.insert(sink.end(), tmp, tmp + r);
        read_total += r;
    }

    EXPECT_EQ(read_total, source.size());
    EXPECT_EQ(sink.size(), source.size());
    EXPECT_EQ(sink, source);
}

// -----------------------------------------------------------------
// DefaultCapacity - Verify default 128 KB template compiles and works
// -----------------------------------------------------------------
TEST(RingBuffer, DefaultCapacity) {
    // Just verify it compiles and basic operations work.
    // Allocate on heap to avoid stack overflow from 128 KB array.
    auto ring = std::make_unique<DefaultRing>();
    EXPECT_EQ(ring->readAvailable(), 0u);
    EXPECT_EQ(DefaultRing::kCapacity, 128u * 1024u);

    const char* msg = "test";
    EXPECT_EQ(ring->write(msg, 4), 4u);
    char buf[8];
    EXPECT_EQ(ring->read(buf, sizeof(buf)), 4u);
    EXPECT_EQ(std::string(buf, 4), "test");
}

} // namespace
