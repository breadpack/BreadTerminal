#include "bench_vt_parser.h"
#include "termcore/vt_parser.h"
#include "termcore/screen.h"

namespace bench {

/// Minimal handler that counts events without doing screen work.
class NullHandler : public termcore::VtParserHandler {
public:
    uint64_t print_count = 0;
    uint64_t execute_count = 0;
    uint64_t csi_count = 0;
    uint64_t osc_count = 0;
    uint64_t esc_count = 0;
    uint64_t dcs_count = 0;

    void onPrint(char32_t) override { ++print_count; }
    void onExecute(uint8_t) override { ++execute_count; }
    void onCsiDispatch(char32_t, const std::vector<termcore::VtParam>&,
                       const std::string&) override { ++csi_count; }
    void onEscDispatch(char32_t, const std::string&) override { ++esc_count; }
    void onOscDispatch(int, const std::string&) override { ++osc_count; }
    void onDcsDispatch(char32_t, const std::vector<termcore::VtParam>&,
                       const std::string&, const std::string&) override { ++dcs_count; }

    void reset() {
        print_count = execute_count = csi_count = osc_count = esc_count = dcs_count = 0;
    }
};

void runVtParserBenchmarks(BenchmarkRunner& runner) {
    // --- Plain text throughput (parser only, no screen) ---
    {
        constexpr size_t data_size = 4 * 1024 * 1024; // 4 MB
        std::string data = generateAsciiData(data_size);

        runner.run("plain_text_throughput", "MB/s", [&]() -> double {
            NullHandler handler;
            termcore::VtParser parser(handler);
            BenchmarkTimer t;
            t.start();
            parser.feed(data.data(), data.size());
            double sec = t.elapsedSec();
            return (static_cast<double>(data_size) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- Color text throughput (parser only) ---
    {
        constexpr size_t data_size = 4 * 1024 * 1024;
        std::string data = generateColorData(data_size);

        runner.run("color_text_throughput", "MB/s", [&]() -> double {
            NullHandler handler;
            termcore::VtParser parser(handler);
            BenchmarkTimer t;
            t.start();
            parser.feed(data.data(), data.size());
            double sec = t.elapsedSec();
            return (static_cast<double>(data.size()) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- Complex escape sequences throughput ---
    {
        constexpr size_t data_size = 2 * 1024 * 1024;
        std::string data = generateComplexEscapeData(data_size);

        runner.run("complex_escape_throughput", "MB/s", [&]() -> double {
            NullHandler handler;
            termcore::VtParser parser(handler);
            BenchmarkTimer t;
            t.start();
            parser.feed(data.data(), data.size());
            double sec = t.elapsedSec();
            return (static_cast<double>(data.size()) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- Unicode throughput (parser only) ---
    {
        constexpr size_t data_size = 2 * 1024 * 1024;
        std::string data = generateUnicodeData(data_size);

        runner.run("unicode_throughput", "MB/s", [&]() -> double {
            NullHandler handler;
            termcore::VtParser parser(handler);
            BenchmarkTimer t;
            t.start();
            parser.feed(data.data(), data.size());
            double sec = t.elapsedSec();
            return (static_cast<double>(data.size()) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- Plain text throughput (parser + screen) ---
    {
        constexpr size_t data_size = 2 * 1024 * 1024;
        std::string data = generateAsciiData(data_size);

        runner.run("plain_text_with_screen", "MB/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            termcore::VtParser parser(screen);
            BenchmarkTimer t;
            t.start();
            parser.feed(data.data(), data.size());
            double sec = t.elapsedSec();
            return (static_cast<double>(data_size) / (1024.0 * 1024.0)) / sec;
        });
    }

    // --- Scrollback fill performance ---
    {
        // Generate enough data to fill scrollback (10000 lines default)
        constexpr int num_lines = 12000;
        std::string data;
        data.reserve(num_lines * 82);
        for (int i = 0; i < num_lines; ++i) {
            for (int j = 0; j < 78; ++j)
                data.push_back(static_cast<char>(65 + ((i + j) % 26)));
            data += "\r\n";
        }

        runner.run("scrollback_fill", "MB/s", [&]() -> double {
            termcore::Screen screen(24, 80);
            screen.setMaxScrollback(10000);
            termcore::VtParser parser(screen);
            BenchmarkTimer t;
            t.start();
            parser.feed(data.data(), data.size());
            double sec = t.elapsedSec();
            return (static_cast<double>(data.size()) / (1024.0 * 1024.0)) / sec;
        });
    }
}

} // namespace bench
