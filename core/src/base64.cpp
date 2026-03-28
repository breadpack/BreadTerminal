#include "termcore/base64.h"

#include <array>
#include <cstring>

namespace termcore {

// ---------------------------------------------------------------------------
// Decode table: maps every possible byte value to its 6-bit payload.
// Valid base64 characters map to 0-63.  '=' maps to -2 (padding sentinel).
// Whitespace (CR, LF, space, tab) maps to -3 (skip sentinel).
// Everything else maps to -1 (invalid).
// ---------------------------------------------------------------------------
static constexpr int8_t kInvalid  = -1;
static constexpr int8_t kPadding  = -2;
static constexpr int8_t kSkip     = -3;

static constexpr std::array<int8_t, 256> buildDecodeTable() {
    std::array<int8_t, 256> t{};
    for (auto& v : t) v = kInvalid;

    // Standard base64 alphabet
    for (int i = 0; i < 26; ++i) t[static_cast<uint8_t>('A' + i)] = static_cast<int8_t>(i);
    for (int i = 0; i < 26; ++i) t[static_cast<uint8_t>('a' + i)] = static_cast<int8_t>(26 + i);
    for (int i = 0; i < 10; ++i) t[static_cast<uint8_t>('0' + i)] = static_cast<int8_t>(52 + i);
    t[static_cast<uint8_t>('+')] = 62;
    t[static_cast<uint8_t>('/')] = 63;

    // Padding
    t[static_cast<uint8_t>('=')] = kPadding;

    // Whitespace to skip
    t[static_cast<uint8_t>('\n')] = kSkip;
    t[static_cast<uint8_t>('\r')] = kSkip;
    t[static_cast<uint8_t>(' ')]  = kSkip;
    t[static_cast<uint8_t>('\t')] = kSkip;

    return t;
}

static constexpr auto kDecodeTable = buildDecodeTable();

// ---------------------------------------------------------------------------
// base64Decode  -- fast path processes 4 valid base64 chars at a time
// ---------------------------------------------------------------------------
std::vector<uint8_t> base64Decode(const std::string& input) {
    if (input.empty()) return {};

    std::vector<uint8_t> result;
    result.reserve(input.size() * 3 / 4);

    const auto* src = reinterpret_cast<const uint8_t*>(input.data());
    const size_t len = input.size();

    // Accumulator: we collect 4 valid 6-bit values, then emit 3 bytes.
    uint32_t accum = 0;
    int count = 0;       // number of valid 6-bit values accumulated (0-4)
    int padding = 0;     // number of '=' seen in current group

    // Fast scan: try to grab 4 valid base64 chars at a time from the input.
    // When the input has no whitespace (common for image data), this inner
    // loop processes 4 input bytes per iteration with minimal branching.
    size_t i = 0;
    while (i < len) {
        // --- fast path: try to consume 4 consecutive valid chars ----------
        if (count == 0 && i + 3 < len) {
            int8_t a = kDecodeTable[src[i]];
            int8_t b = kDecodeTable[src[i + 1]];
            int8_t c = kDecodeTable[src[i + 2]];
            int8_t d = kDecodeTable[src[i + 3]];

            // All four are valid base64 data chars (0..63)?
            if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
                uint32_t triple = (static_cast<uint32_t>(a) << 18)
                                | (static_cast<uint32_t>(b) << 12)
                                | (static_cast<uint32_t>(c) << 6)
                                | static_cast<uint32_t>(d);
                result.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
                result.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
                result.push_back(static_cast<uint8_t>(triple & 0xFF));
                i += 4;
                continue;
            }

            // Check for the common case: 2 data + padding (==)
            if (a >= 0 && b >= 0 && c == kPadding && d == kPadding) {
                uint32_t triple = (static_cast<uint32_t>(a) << 18)
                                | (static_cast<uint32_t>(b) << 12);
                result.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
                i += 4;
                continue;
            }

            // Check for 3 data + 1 padding (=)
            if (a >= 0 && b >= 0 && c >= 0 && d == kPadding) {
                uint32_t triple = (static_cast<uint32_t>(a) << 18)
                                | (static_cast<uint32_t>(b) << 12)
                                | (static_cast<uint32_t>(c) << 6);
                result.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
                result.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
                i += 4;
                continue;
            }
        }

        // --- slow path: process one byte at a time (whitespace, etc.) -----
        int8_t v = kDecodeTable[src[i]];
        ++i;

        if (v >= 0) {
            // Valid data char
            accum = (accum << 6) | static_cast<uint32_t>(v);
            ++count;
            if (count == 4) {
                result.push_back(static_cast<uint8_t>((accum >> 16) & 0xFF));
                if (padding < 2)
                    result.push_back(static_cast<uint8_t>((accum >> 8) & 0xFF));
                if (padding < 1)
                    result.push_back(static_cast<uint8_t>(accum & 0xFF));
                accum = 0;
                count = 0;
                padding = 0;
            }
        } else if (v == kPadding) {
            accum <<= 6;
            ++count;
            ++padding;
            if (count == 4) {
                result.push_back(static_cast<uint8_t>((accum >> 16) & 0xFF));
                if (padding < 2)
                    result.push_back(static_cast<uint8_t>((accum >> 8) & 0xFF));
                if (padding < 1)
                    result.push_back(static_cast<uint8_t>(accum & 0xFF));
                accum = 0;
                count = 0;
                padding = 0;
            }
        }
        // else: kSkip or kInvalid -- just skip
    }

    // Handle trailing data without padding (non-strict mode, common in Kitty protocol)
    if (count == 3) {
        accum <<= 6;
        result.push_back(static_cast<uint8_t>((accum >> 16) & 0xFF));
        result.push_back(static_cast<uint8_t>((accum >> 8) & 0xFF));
    } else if (count == 2) {
        accum <<= 12;
        result.push_back(static_cast<uint8_t>((accum >> 16) & 0xFF));
    }

    return result;
}

// ---------------------------------------------------------------------------
// base64DecodeToString
// ---------------------------------------------------------------------------
std::string base64DecodeToString(const std::string& input) {
    auto bytes = base64Decode(input);
    return std::string(bytes.begin(), bytes.end());
}

// ---------------------------------------------------------------------------
// base64Encode
// ---------------------------------------------------------------------------
static constexpr char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 2 < len) {
        uint32_t triplet = (static_cast<uint32_t>(data[i]) << 16)
                         | (static_cast<uint32_t>(data[i + 1]) << 8)
                         | static_cast<uint32_t>(data[i + 2]);
        result += kEncodeTable[(triplet >> 18) & 0x3F];
        result += kEncodeTable[(triplet >> 12) & 0x3F];
        result += kEncodeTable[(triplet >> 6) & 0x3F];
        result += kEncodeTable[triplet & 0x3F];
        i += 3;
    }

    if (i + 1 == len) {
        uint32_t val = static_cast<uint32_t>(data[i]) << 16;
        result += kEncodeTable[(val >> 18) & 0x3F];
        result += kEncodeTable[(val >> 12) & 0x3F];
        result += '=';
        result += '=';
    } else if (i + 2 == len) {
        uint32_t val = (static_cast<uint32_t>(data[i]) << 16)
                     | (static_cast<uint32_t>(data[i + 1]) << 8);
        result += kEncodeTable[(val >> 18) & 0x3F];
        result += kEncodeTable[(val >> 12) & 0x3F];
        result += kEncodeTable[(val >> 6) & 0x3F];
        result += '=';
    }

    return result;
}

} // namespace termcore
