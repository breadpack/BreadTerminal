#ifndef TERMCORE_RING_BUFFER_H
#define TERMCORE_RING_BUFFER_H

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace termcore {

/// Thread-safe (single-producer single-consumer) ring buffer for PTY output.
/// Uses monotonically increasing read/write positions to avoid ambiguity
/// between full and empty states.  Capacity is a template parameter so the
/// internal array is embedded (no heap allocation).
template <size_t Capacity = 128 * 1024>
class RingBuffer {
public:
    static constexpr size_t kCapacity = Capacity;

    /// Number of bytes available to read.
    size_t readAvailable() const {
        return write_pos_ - read_pos_;
    }

    /// Write data into the ring buffer.  Returns number of bytes written.
    size_t write(const char* data, size_t len) {
        size_t avail = kCapacity - readAvailable();
        if (len > avail) len = avail;
        if (len == 0) return 0;

        size_t wpos = write_pos_ % kCapacity;
        size_t first = (std::min)(len, kCapacity - wpos);
        std::memcpy(buf_ + wpos, data, first);
        if (first < len) {
            std::memcpy(buf_, data + first, len - first);
        }
        write_pos_ += len;
        return len;
    }

    /// Read data out of the ring buffer.  Returns number of bytes read.
    size_t read(char* dst, size_t len) {
        size_t avail = readAvailable();
        if (len > avail) len = avail;
        if (len == 0) return 0;

        size_t rpos = read_pos_ % kCapacity;
        size_t first = (std::min)(len, kCapacity - rpos);
        std::memcpy(dst, buf_ + rpos, first);
        if (first < len) {
            std::memcpy(dst + first, buf_, len - first);
        }
        read_pos_ += len;
        return len;
    }

private:
    char buf_[kCapacity]{};
    size_t write_pos_ = 0;
    size_t read_pos_ = 0;
};

} // namespace termcore

#endif // TERMCORE_RING_BUFFER_H
