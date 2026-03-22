#include "termcore/compressed_row.h"

namespace termcore {

static bool cellsEqual(const TermCell& a, const TermCell& b) {
    if (a.codepoint != b.codepoint ||
        a.fg_color != b.fg_color ||
        a.bg_color != b.bg_color ||
        a.attributes != b.attributes ||
        a.width != b.width ||
        a.underline_style != b.underline_style ||
        a.underline_color != b.underline_color ||
        a.extra_count != b.extra_count)
        return false;
    for (uint8_t i = 0; i < a.extra_count; ++i) {
        if (a.extra[i] != b.extra[i]) return false;
    }
    return true;
}

void CompressedRow::compress(const std::vector<TermCell>& row) {
    runs_.clear();
    if (row.empty()) return;

    CellRun current;
    current.cell = row[0];
    current.count = 1;

    for (size_t i = 1; i < row.size(); ++i) {
        if (cellsEqual(row[i], current.cell) && current.count < 65535) {
            current.count++;
        } else {
            runs_.push_back(current);
            current.cell = row[i];
            current.count = 1;
        }
    }
    runs_.push_back(current);
    runs_.shrink_to_fit();
}

void CompressedRow::decompress(std::vector<TermCell>& out, int cols) const {
    out.resize(cols);
    int pos = 0;
    for (const auto& run : runs_) {
        for (uint16_t j = 0; j < run.count && pos < cols; ++j) {
            out[pos++] = run.cell;
        }
    }
    // Fill remaining cells with defaults if compressed data is shorter
    for (; pos < cols; ++pos) {
        out[pos] = TermCell{};
    }
}

TermCell CompressedRow::cellAt(int col) const {
    int pos = 0;
    for (const auto& run : runs_) {
        if (col < pos + run.count) {
            return run.cell;
        }
        pos += run.count;
    }
    return TermCell{};  // Out of bounds
}

static void appendCodepointUtf8(std::string& result, char32_t cp) {
    if (cp < 0x80) {
        result += static_cast<char>(cp);
    } else if (cp < 0x800) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string CompressedRow::text(int cols) const {
    std::string result;
    int pos = 0;
    for (const auto& run : runs_) {
        int run_end = pos + run.count;
        if (run_end > cols) run_end = cols;
        int count = run_end - pos;
        if (count <= 0) break;

        for (int j = 0; j < count; ++j) {
            if (run.cell.codepoint == 0 && run.cell.width == 0) continue;
            appendCodepointUtf8(result, run.cell.codepoint);
            for (uint8_t ei = 0; ei < run.cell.extra_count; ++ei) {
                appendCodepointUtf8(result, run.cell.extra[ei]);
            }
        }
        pos = run_end;
        if (pos >= cols) break;
    }
    // Trim trailing spaces (matching getScrollbackLineText behavior)
    auto trim_pos = result.find_last_not_of(' ');
    if (trim_pos != std::string::npos) {
        result.erase(trim_pos + 1);
    } else {
        result.clear();
    }
    return result;
}

int CompressedRow::totalCols() const {
    int total = 0;
    for (const auto& run : runs_) {
        total += run.count;
    }
    return total;
}

size_t CompressedRow::memoryUsage() const {
    return sizeof(CompressedRow) + runs_.capacity() * sizeof(CellRun);
}

} // namespace termcore
