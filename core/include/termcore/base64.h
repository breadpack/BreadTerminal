#ifndef TERMCORE_BASE64_H
#define TERMCORE_BASE64_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

/// Fast Base64 decode using a 256-entry lookup table and 4-byte-at-a-time
/// processing.  Handles standard alphabet (A-Z, a-z, 0-9, +, /), padding
/// with '=', and embedded whitespace (CR/LF).  Returns empty vector on
/// empty input.
std::vector<uint8_t> base64Decode(const std::string& input);

/// Convenience overload that returns a std::string instead of byte vector.
std::string base64DecodeToString(const std::string& input);

/// Encode raw bytes to a Base64 string (standard alphabet, with padding).
std::string base64Encode(const uint8_t* data, size_t len);

/// Convenience overload for vector input.
inline std::string base64Encode(const std::vector<uint8_t>& data) {
    return base64Encode(data.data(), data.size());
}

/// Convenience overload for string input.
inline std::string base64Encode(const std::string& data) {
    return base64Encode(reinterpret_cast<const uint8_t*>(data.data()),
                        data.size());
}

} // namespace termcore

#endif // TERMCORE_BASE64_H
