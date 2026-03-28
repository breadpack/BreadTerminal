#ifndef BENCHMARK_COMMON_H
#define BENCHMARK_COMMON_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

namespace bench {

/// High-resolution timer for benchmarking.
class BenchmarkTimer {
public:
    void start() {
        start_ = std::chrono::high_resolution_clock::now();
    }

    double elapsedMs() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }

    double elapsedUs() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::micro>(now - start_).count();
    }

    double elapsedSec() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(now - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

/// Statistical result of a benchmark run.
struct BenchmarkResult {
    std::string suite;
    std::string name;
    std::string unit;
    int iterations = 0;
    double mean = 0;
    double median = 0;
    double min = 0;
    double max = 0;
    double stddev = 0;

    /// Compute stats from raw measurements.
    static BenchmarkResult compute(const std::string& suite,
                                    const std::string& name,
                                    const std::string& unit,
                                    std::vector<double>& values) {
        BenchmarkResult r;
        r.suite = suite;
        r.name = name;
        r.unit = unit;
        r.iterations = static_cast<int>(values.size());

        if (values.empty()) return r;

        std::sort(values.begin(), values.end());
        r.min = values.front();
        r.max = values.back();

        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        r.mean = sum / values.size();

        size_t mid = values.size() / 2;
        r.median = (values.size() % 2 == 0)
            ? (values[mid - 1] + values[mid]) / 2.0
            : values[mid];

        double sq_sum = 0;
        for (double v : values)
            sq_sum += (v - r.mean) * (v - r.mean);
        r.stddev = std::sqrt(sq_sum / values.size());

        return r;
    }
};

/// Collects and runs benchmarks with warmup and measurement iterations.
class BenchmarkRunner {
public:
    explicit BenchmarkRunner(const std::string& suite_name,
                              int warmup = 3, int iterations = 10)
        : suite_(suite_name), warmup_(warmup), iterations_(iterations) {}

    /// Run a benchmark that returns a single metric value per iteration.
    /// The function should perform the work and return the measured value
    /// (e.g., MB/s, ops/sec).
    void run(const std::string& name, const std::string& unit,
             std::function<double()> fn) {
        // Warmup
        for (int i = 0; i < warmup_; ++i) {
            fn();
        }

        // Measure
        std::vector<double> values;
        values.reserve(iterations_);
        for (int i = 0; i < iterations_; ++i) {
            values.push_back(fn());
        }

        results_.push_back(
            BenchmarkResult::compute(suite_, name, unit, values));
    }

    /// Run a benchmark that measures elapsed time.
    /// Reports time in the specified unit (ms or us).
    void runTimed(const std::string& name, const std::string& unit,
                  std::function<void()> fn) {
        bool use_us = (unit == "us" || unit == "microseconds");

        // Warmup
        for (int i = 0; i < warmup_; ++i) {
            fn();
        }

        // Measure
        std::vector<double> values;
        values.reserve(iterations_);
        for (int i = 0; i < iterations_; ++i) {
            BenchmarkTimer t;
            t.start();
            fn();
            values.push_back(use_us ? t.elapsedUs() : t.elapsedMs());
        }

        results_.push_back(
            BenchmarkResult::compute(suite_, name, unit, values));
    }

    const std::vector<BenchmarkResult>& results() const { return results_; }
    const std::string& suite() const { return suite_; }

    void setWarmup(int w) { warmup_ = w; }
    void setIterations(int n) { iterations_ = n; }

private:
    std::string suite_;
    int warmup_;
    int iterations_;
    std::vector<BenchmarkResult> results_;
};

/// Generate a string of N bytes of printable ASCII (for throughput tests).
inline std::string generateAsciiData(size_t bytes) {
    std::string data;
    data.reserve(bytes);
    for (size_t i = 0; i < bytes; ++i) {
        // Printable ASCII 32-126, with occasional newlines
        if (i % 80 == 79) {
            data.push_back('\n');
        } else {
            data.push_back(static_cast<char>(32 + (i % 95)));
        }
    }
    return data;
}

/// Generate text with SGR color sequences interspersed.
inline std::string generateColorData(size_t target_bytes) {
    std::string data;
    data.reserve(target_bytes + target_bytes / 4);
    size_t written = 0;
    int color = 31;
    while (written < target_bytes) {
        // Insert SGR color every ~20 chars
        std::string sgr = "\033[" + std::to_string(color) + "m";
        data += sgr;
        written += sgr.size();
        for (int j = 0; j < 20 && written < target_bytes; ++j) {
            if (written % 80 == 79) {
                data.push_back('\n');
            } else {
                data.push_back(static_cast<char>(65 + (written % 26)));
            }
            ++written;
        }
        color = 31 + ((color - 30) % 7);
    }
    data += "\033[0m";
    return data;
}

/// Generate text with complex escape sequences (CSI, OSC, DCS mix).
inline std::string generateComplexEscapeData(size_t target_bytes) {
    std::string data;
    data.reserve(target_bytes);
    size_t written = 0;

    const std::string sequences[] = {
        "\033[1;31m",         // Bold red
        "\033[38;2;100;200;50m", // 24-bit fg color
        "\033[48;5;240m",     // 256-color bg
        "\033[H",             // Cursor home
        "\033[2J",            // Clear screen
        "\033[10;20H",        // Cursor position
        "\033[?25h",          // Show cursor
        "\033[4m",            // Underline
        "\033[0m",            // Reset
        "\033]0;Title\007",   // OSC set title
        "\033[1A",            // Cursor up
        "\033[5B",            // Cursor down 5
        "\033[K",             // Erase to end of line
        "\033[1;1;80;24r",    // Set scroll region
    };
    constexpr int num_seqs = sizeof(sequences) / sizeof(sequences[0]);

    int idx = 0;
    while (written < target_bytes) {
        const auto& seq = sequences[idx % num_seqs];
        data += seq;
        written += seq.size();
        // Some printable text between sequences
        for (int j = 0; j < 10 && written < target_bytes; ++j) {
            data.push_back(static_cast<char>(65 + (written % 26)));
            ++written;
        }
        ++idx;
    }
    return data;
}

/// Generate Unicode-heavy text (CJK, emoji, combining characters).
inline std::string generateUnicodeData(size_t target_bytes) {
    std::string data;
    data.reserve(target_bytes);

    // CJK characters (U+4E00 - U+9FFF), 3 bytes each in UTF-8
    // Emoji (various), 4 bytes each
    // Latin with combining marks

    const std::string fragments[] = {
        "\xe4\xb8\xad\xe6\x96\x87",         // Chinese: "zhongwen"
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", // Japanese: "nihongo"
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4", // Korean: "hangugeo"
        "\xf0\x9f\x98\x80",                   // Emoji: grinning face
        "\xf0\x9f\x8e\x89",                   // Emoji: party popper
        "e\xcc\x81",                           // e + combining acute
        "n\xcc\x83",                           // n + combining tilde
        "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb", // Man technologist (ZWJ)
    };
    constexpr int num_frags = sizeof(fragments) / sizeof(fragments[0]);

    size_t written = 0;
    int idx = 0;
    while (written < target_bytes) {
        const auto& frag = fragments[idx % num_frags];
        if (written + frag.size() > target_bytes) break;
        data += frag;
        written += frag.size();
        ++idx;
        // Occasional newline
        if (idx % 10 == 0 && written < target_bytes) {
            data.push_back('\n');
            ++written;
        }
    }
    return data;
}

/// Generate short colored lines simulating "ls -la" output.
inline std::string generateLsOutput(int num_lines) {
    std::string data;
    data.reserve(num_lines * 80);
    for (int i = 0; i < num_lines; ++i) {
        // Permission string
        data += "\033[1;34m";
        data += "drwxr-xr-x";
        data += "\033[0m  ";
        // User/group
        data += "user  group  ";
        // Size
        data += "\033[1;32m";
        data += std::to_string(1024 + (i * 137) % 99999);
        data += "\033[0m ";
        // Date
        data += "Mar 22 10:";
        data += std::to_string(10 + i % 50);
        data += " ";
        // Filename
        data += "\033[1;36m";
        data += "file_" + std::to_string(i) + ".txt";
        data += "\033[0m\n";
    }
    return data;
}

/// Generate a full-screen rewrite pattern (like vim redraw).
inline std::string generateVimRedraw(int rows, int cols) {
    std::string data;
    data.reserve(rows * (cols + 20));
    for (int r = 0; r < rows; ++r) {
        // Move cursor to row, col 1
        data += "\033[" + std::to_string(r + 1) + ";1H";
        // Clear line
        data += "\033[2K";
        // Write content with syntax highlighting colors
        if (r == 0) {
            // Status line
            data += "\033[7m"; // Reverse video
            for (int c = 0; c < cols; ++c)
                data.push_back(' ');
            data += "\033[0m";
        } else {
            // Line number
            data += "\033[33m";
            data += std::to_string(r);
            data += "\033[0m ";
            // Code content
            data += "\033[38;5;81m";
            int remaining = cols - 6;
            for (int c = 0; c < remaining; ++c)
                data.push_back(static_cast<char>(65 + (c % 26)));
            data += "\033[0m";
        }
    }
    return data;
}

/// Generate pure printable ASCII with no newlines or control characters.
/// Useful for measuring the SIMD fast-path without any breaks.
inline std::string generatePureAsciiData(size_t bytes) {
    std::string data;
    data.reserve(bytes);
    for (size_t i = 0; i < bytes; ++i) {
        data.push_back(static_cast<char>(0x20 + (i % 95))); // 0x20-0x7E
    }
    return data;
}

/// Generate UTF-8 text using only characters of the given byte width.
/// @param byte_width 1, 2, 3, or 4 bytes per character.
/// @param target_bytes approximate output size in bytes.
inline std::string generateUtf8ByWidth(int byte_width, size_t target_bytes) {
    std::string data;
    data.reserve(target_bytes + 4);
    size_t written = 0;
    switch (byte_width) {
        case 1:
            // ASCII printable: 0x20-0x7E
            while (written < target_bytes) {
                data.push_back(static_cast<char>(0x20 + (written % 95)));
                ++written;
            }
            break;
        case 2:
            // Latin Extended: U+00C0-U+00FF (2-byte UTF-8: 0xC3 0x80-0xBF)
            while (written + 2 <= target_bytes) {
                char32_t cp = 0x00C0 + (written / 2 % 64);
                data.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                data.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                written += 2;
            }
            break;
        case 3:
            // CJK Unified: U+4E00-U+9FFF (3-byte UTF-8)
            while (written + 3 <= target_bytes) {
                char32_t cp = 0x4E00 + (written / 3 % 0x5200);
                data.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                data.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                data.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                written += 3;
            }
            break;
        case 4:
            // Emoji/Supplementary: U+1F600-U+1F64F (4-byte UTF-8)
            while (written + 4 <= target_bytes) {
                char32_t cp = 0x1F600 + (written / 4 % 80);
                data.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                data.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                data.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                data.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                written += 4;
            }
            break;
    }
    return data;
}

/// Generate a CSI SGR sequence with the given number of parameters.
/// Returns something like "\033[1;31;42;...m".
inline std::string generateCsiWithParams(int param_count) {
    std::string seq = "\033[";
    for (int i = 0; i < param_count; ++i) {
        if (i > 0) seq += ';';
        seq += std::to_string(i % 108); // valid SGR param range
    }
    seq += 'm';
    return seq;
}

/// Generate data consisting of repeated CSI sequences with the given param count,
/// interspersed with short text runs.
inline std::string generateCsiParamData(int param_count, size_t target_bytes) {
    std::string csi = generateCsiWithParams(param_count);
    std::string data;
    data.reserve(target_bytes + csi.size());
    size_t written = 0;
    while (written < target_bytes) {
        data += csi;
        written += csi.size();
        // Short text between sequences
        for (int j = 0; j < 10 && written < target_bytes; ++j) {
            data.push_back(static_cast<char>(0x41 + (written % 26)));
            ++written;
        }
    }
    return data;
}

/// Generate realistic colored `ls --color` output.
/// Each line has: color code + permissions + reset + user/group + color + filename + reset + newline.
inline std::string generateRealisticLsColorOutput(int num_files) {
    // File type colors matching real ls --color defaults
    const char* type_colors[] = {
        "\033[0;34m",        // directory (blue)
        "\033[0m",           // regular file (default)
        "\033[0;32m",        // executable (green)
        "\033[0;36m",        // symlink (cyan)
        "\033[1;31m",        // archive (bold red)
        "\033[0;35m",        // image (magenta)
    };
    const char* perms[] = {
        "drwxr-xr-x", "-rw-r--r--", "-rwxr-xr-x",
        "lrwxrwxrwx", "-rw-r--r--", "-rw-r--r--",
    };
    const char* extensions[] = {
        "", ".cpp", ".sh", " -> target", ".tar.gz", ".png",
    };
    const char* names[] = {
        "src", "main", "build", "lib", "backup", "screenshot",
    };

    std::string data;
    data.reserve(num_files * 120);
    // header line like "total 1234"
    data += "total " + std::to_string(num_files * 4) + "\n";

    for (int i = 0; i < num_files; ++i) {
        int type = i % 6;
        // permissions
        data += type_colors[type];
        data += perms[type];
        data += "\033[0m ";
        // links + user + group
        data += " 1 user  group  ";
        // size (right-aligned feel)
        data += "\033[0;33m";
        data += std::to_string(64 + (i * 137) % 999999);
        data += "\033[0m ";
        // date
        data += "Mar 28 ";
        data += std::to_string(10 + i % 14);
        data += ":";
        data += std::to_string(10 + i % 50);
        data += " ";
        // colored filename
        data += type_colors[type];
        data += names[type];
        data += "_" + std::to_string(i);
        data += extensions[type];
        data += "\033[0m\n";
    }
    return data;
}

/// Generate realistic vim screen redraw with cursor movements, SGR, and line numbers.
/// Simulates a full-screen repaint with syntax-highlighted code.
inline std::string generateRealisticVimRedraw(int rows, int cols) {
    // Keyword colors for pseudo-syntax highlighting
    const char* kw_color = "\033[0;33m";     // yellow: keywords
    const char* str_color = "\033[0;32m";     // green: strings
    const char* comment_color = "\033[0;36m"; // cyan: comments
    const char* num_color = "\033[0;35m";     // magenta: numbers
    const char* fn_color = "\033[1;34m";      // bold blue: functions
    const char* reset = "\033[0m";
    const char* line_nr_color = "\033[0;90m"; // dark gray: line numbers
    const char* status_bg = "\033[48;5;236m\033[38;5;252m"; // dark bg, light fg

    // Simulated code lines (cycling)
    struct CodeLine {
        const char* color;
        const char* text;
    };
    const CodeLine code_lines[] = {
        { kw_color,      "int " },
        { fn_color,      "main" },
        { reset,         "() {" },
        { comment_color, "    // Initialize the system" },
        { kw_color,      "    auto " },
        { reset,         "result = " },
        { fn_color,      "initialize" },
        { reset,         "();" },
        { kw_color,      "    if " },
        { reset,         "(result == " },
        { num_color,     "0" },
        { reset,         ") {" },
        { str_color,     "        \"Success\"" },
        { reset,         ";" },
        { reset,         "    }" },
        { kw_color,      "    return " },
        { num_color,     "0" },
        { reset,         ";" },
        { reset,         "}" },
    };
    constexpr int num_code_lines = sizeof(code_lines) / sizeof(code_lines[0]);

    std::string data;
    data.reserve(rows * (cols + 80));

    // Hide cursor during redraw
    data += "\033[?25l";

    for (int r = 0; r < rows; ++r) {
        // CUP: move cursor to row r+1, col 1
        data += "\033[" + std::to_string(r + 1) + ";1H";
        // Erase line
        data += "\033[2K";

        if (r == rows - 1) {
            // Status line (last row)
            data += status_bg;
            std::string status = " NORMAL | main.cpp | Ln " + std::to_string(r) + ", Col 1";
            data += status;
            // Pad to fill
            for (int c = static_cast<int>(status.size()); c < cols; ++c)
                data.push_back(' ');
            data += reset;
        } else if (r == rows - 2) {
            // Command line (empty)
            data += ":";
        } else {
            // Line number gutter (5 chars wide)
            data += line_nr_color;
            std::string ln = std::to_string(r + 1);
            for (int p = 0; p < 4 - static_cast<int>(ln.size()); ++p)
                data.push_back(' ');
            data += ln;
            data += reset;
            data += " ";

            // Code content
            if (r < num_code_lines) {
                data += code_lines[r].color;
                data += code_lines[r].text;
                data += reset;
            } else {
                // Tilde for empty lines beyond file content
                data += line_nr_color;
                data += "~";
                data += reset;
            }
        }
    }

    // Show cursor again
    data += "\033[?25h";
    // Position cursor at a typical editing position
    data += "\033[5;10H";

    return data;
}

} // namespace bench

#endif // BENCHMARK_COMMON_H
