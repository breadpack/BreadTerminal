#ifndef TERMCORE_VT_PARSER_SIMD_H
#define TERMCORE_VT_PARSER_SIMD_H

#include <cstddef>
#include <cstdint>

namespace termcore {

/// Scan forward through `data` returning the number of consecutive
/// printable ASCII bytes (0x20-0x7E).  Uses SSE2/AVX2 when available,
/// with runtime CPU detection and scalar fallback.
size_t scanPrintableAscii(const uint8_t* data, size_t len);

} // namespace termcore

#endif // TERMCORE_VT_PARSER_SIMD_H
