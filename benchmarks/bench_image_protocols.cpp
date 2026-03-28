#include "bench_image_protocols.h"
#include "benchmark_image_helpers.h"
#include "termcore/sixel.h"
#include "termcore/kitty_graphics.h"
#include "termcore/iterm_image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bench {

void runImageProtocolBenchmarks(BenchmarkRunner& runner) {

    // =========================================================================
    // Sixel Benchmarks
    // =========================================================================

    // sixel_parse_simple: Parse a simple single-color 80x24-pixel sixel image
    {
        // 80 pixels wide, 4 bands = 24 pixels tall
        std::string sixel_data = generateSimpleSixel(80, 4);

        runner.run("sixel_parse_simple", "ops/sec", [&]() -> double {
            constexpr int ops = 1000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                auto img = termcore::parseSixel(sixel_data);
                (void)img;
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }

    // sixel_parse_complex: Parse multi-color sixel with repeats (160x60, 64 colors)
    {
        // 160 pixels wide, 10 bands = 60 pixels tall, 64 colors
        std::string sixel_data = generateComplexSixel(160, 10, 64);

        runner.run("sixel_parse_complex", "ops/sec", [&]() -> double {
            constexpr int ops = 50;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                auto img = termcore::parseSixel(sixel_data);
                (void)img;
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }

    // sixel_parse_large: Parse large sixel data (throughput test)
    {
        constexpr size_t target_size = 16 * 1024; // 16 KB of sixel data
        std::string sixel_data = generateLargeSixel(target_size);

        runner.run("sixel_parse_large", "KB/s", [&]() -> double {
            BenchmarkTimer t;
            t.start();
            auto img = termcore::parseSixel(sixel_data);
            (void)img;
            double sec = t.elapsedSec();
            return (static_cast<double>(sixel_data.size()) / 1024.0) / sec;
        });
    }

    // =========================================================================
    // Kitty Graphics Benchmarks
    // =========================================================================

    // kitty_base64_decode: Base64 decode throughput (1 MB payload)
    {
        constexpr size_t raw_size = 1024 * 1024; // 1 MB raw -> ~1.33 MB base64
        std::string b64_payload = generateBase64Payload(raw_size);

        runner.run("kitty_base64_decode", "MB/s", [&]() -> double {
            // Use iTermBase64Decode as the public base64 decode function
            BenchmarkTimer t;
            t.start();
            auto decoded = termcore::iTermBase64Decode(b64_payload);
            (void)decoded;
            double sec = t.elapsedSec();
            return (static_cast<double>(b64_payload.size()) / (1024.0 * 1024.0)) / sec;
        });
    }

    // kitty_command_parse: Control string parsing throughput
    {
        // Generate many control strings to parse via processCommand
        std::vector<std::string> controls;
        std::vector<std::string> payloads;
        constexpr int count = 500;
        controls.reserve(count);
        payloads.reserve(count);

        // Small 4x4 RGBA image = 64 bytes raw
        std::vector<uint8_t> small_img(64, 0xAA);
        std::string small_b64 = base64Encode(small_img);

        for (int i = 0; i < count; ++i) {
            controls.push_back(generateKittyControl(
                static_cast<uint32_t>(1000 + i), 4, 4, 32));
            payloads.push_back(small_b64);
        }

        runner.run("kitty_command_parse", "ops/sec", [&]() -> double {
            termcore::KittyGraphicsManager mgr;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < count; ++i) {
                mgr.processCommand(controls[i], payloads[i]);
            }
            double sec = t.elapsedSec();
            return count / sec;
        });
    }

    // kitty_transmit_single: Single image transmission (small RGBA data)
    {
        // 64x64 RGBA image = 16384 bytes raw
        constexpr int img_w = 64;
        constexpr int img_h = 64;
        constexpr size_t raw_size = img_w * img_h * 4;
        std::vector<uint8_t> raw(raw_size);
        for (size_t i = 0; i < raw_size; ++i) {
            raw[i] = static_cast<uint8_t>((i * 41 + 7) & 0xFF);
        }
        std::string b64 = base64Encode(raw);
        std::string ctrl = generateKittyControl(1, img_w, img_h, 32);

        runner.run("kitty_transmit_single", "ops/sec", [&]() -> double {
            constexpr int ops = 200;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::KittyGraphicsManager mgr;
                mgr.processCommand(ctrl, b64);
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }

    // kitty_multi_chunk: Multi-chunk transmission and assembly
    {
        // 128x128 RGBA image = 65536 bytes, split into 4 chunks
        constexpr int img_w = 128;
        constexpr int img_h = 128;
        constexpr size_t raw_size = img_w * img_h * 4;
        std::vector<uint8_t> raw(raw_size);
        for (size_t i = 0; i < raw_size; ++i) {
            raw[i] = static_cast<uint8_t>((i * 53 + 11) & 0xFF);
        }
        std::string full_b64 = base64Encode(raw);

        // Split into 4 chunks
        constexpr int num_chunks = 4;
        size_t chunk_size = full_b64.size() / num_chunks;
        std::vector<std::string> chunk_payloads;
        std::vector<std::string> chunk_controls;
        for (int c = 0; c < num_chunks; ++c) {
            size_t start = c * chunk_size;
            size_t len = (c == num_chunks - 1)
                ? (full_b64.size() - start) : chunk_size;
            chunk_payloads.push_back(full_b64.substr(start, len));

            bool more = (c < num_chunks - 1);
            if (c == 0) {
                chunk_controls.push_back(
                    generateKittyControl(42, img_w, img_h, 32, more));
            } else {
                // Continuation chunks: only action and more flag
                std::string ctrl = "a=t,m=" + std::string(more ? "1" : "0");
                chunk_controls.push_back(ctrl);
            }
        }

        runner.run("kitty_multi_chunk", "ops/sec", [&]() -> double {
            constexpr int ops = 200;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < ops; ++i) {
                termcore::KittyGraphicsManager mgr;
                for (int c = 0; c < num_chunks; ++c) {
                    mgr.processCommand(chunk_controls[c], chunk_payloads[c]);
                }
            }
            double sec = t.elapsedSec();
            return ops / sec;
        });
    }

    // =========================================================================
    // iTerm2 Benchmarks
    // =========================================================================

    // iterm_osc_parse: OSC 1337 parameter parsing throughput
    {
        // Generate a variety of OSC strings to parse
        std::string dummy_payload = generateBase64Payload(256);
        std::vector<std::string> osc_strings;
        constexpr int count = 1000;
        osc_strings.reserve(count);

        for (int i = 0; i < count; ++i) {
            std::string width_spec, height_spec;
            switch (i % 4) {
            case 0: width_spec = "auto"; height_spec = "auto"; break;
            case 1: width_spec = "80"; height_spec = "24"; break;
            case 2: width_spec = "640px"; height_spec = "480px"; break;
            case 3: width_spec = "50%"; height_spec = "75%"; break;
            }
            // Base64-encode a simple filename
            std::vector<uint8_t> name_raw = {'i', 'm', 'g', '_',
                static_cast<uint8_t>('0' + (i % 10)), '.', 'p', 'n', 'g'};
            std::string name_b64 = base64Encode(name_raw);

            osc_strings.push_back(generateITermOsc(
                name_b64, 1024 + i, width_spec, height_spec,
                (i % 2 == 0), true, dummy_payload));
        }

        runner.run("iterm_osc_parse", "ops/sec", [&]() -> double {
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < count; ++i) {
                termcore::ITermImageParams params;
                std::string payload;
                termcore::parseITermImageOsc(osc_strings[i], params, payload);
                (void)params;
                (void)payload;
            }
            double sec = t.elapsedSec();
            return count / sec;
        });
    }

    // iterm_dimension_calc: Display cell calculation with aspect ratio
    {
        // Test calculateDisplayCells with various dimension specs
        struct TestCase {
            termcore::ITermImageParams params;
            int image_w, image_h;
        };

        std::vector<TestCase> cases;
        constexpr int num_cases = 500;
        cases.reserve(num_cases);

        for (int i = 0; i < num_cases; ++i) {
            TestCase tc;
            tc.image_w = 100 + (i * 7) % 1920;
            tc.image_h = 100 + (i * 11) % 1080;
            tc.params.preserve_aspect_ratio = (i % 3 != 0);
            tc.params.inline_display = true;

            switch (i % 5) {
            case 0:
                // Both auto
                tc.params.width.unit = termcore::ITermDimension::Unit::Auto;
                tc.params.height.unit = termcore::ITermDimension::Unit::Auto;
                break;
            case 1:
                // Width in cells, height auto
                tc.params.width.unit = termcore::ITermDimension::Unit::Cells;
                tc.params.width.value = 40 + (i % 40);
                tc.params.height.unit = termcore::ITermDimension::Unit::Auto;
                break;
            case 2:
                // Width in pixels, height in pixels
                tc.params.width.unit = termcore::ITermDimension::Unit::Pixels;
                tc.params.width.value = 320 + (i % 640);
                tc.params.height.unit = termcore::ITermDimension::Unit::Pixels;
                tc.params.height.value = 240 + (i % 480);
                break;
            case 3:
                // Percent-based
                tc.params.width.unit = termcore::ITermDimension::Unit::Percent;
                tc.params.width.value = 25 + (i % 75);
                tc.params.height.unit = termcore::ITermDimension::Unit::Percent;
                tc.params.height.value = 25 + (i % 75);
                break;
            case 4:
                // Width auto, height in cells
                tc.params.width.unit = termcore::ITermDimension::Unit::Auto;
                tc.params.height.unit = termcore::ITermDimension::Unit::Cells;
                tc.params.height.value = 10 + (i % 30);
                break;
            }

            cases.push_back(tc);
        }

        runner.run("iterm_dimension_calc", "ops/sec", [&]() -> double {
            constexpr int cell_w = 8;
            constexpr int cell_h = 16;
            constexpr int term_cols = 80;
            constexpr int term_rows = 24;

            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < num_cases; ++i) {
                int out_cols = 0, out_rows = 0;
                termcore::calculateDisplayCells(
                    cases[i].params, cell_w, cell_h,
                    term_cols, term_rows,
                    cases[i].image_w, cases[i].image_h,
                    out_cols, out_rows);
                (void)out_cols;
                (void)out_rows;
            }
            double sec = t.elapsedSec();
            return num_cases / sec;
        });
    }
}

} // namespace bench
