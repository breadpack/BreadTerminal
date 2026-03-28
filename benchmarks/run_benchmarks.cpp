#include "benchmark_common.h"
#include "bench_vt_parser.h"
#include "bench_screen.h"
#include "bench_font.h"
#include "bench_cell_builder.h"
#include "bench_e2e.h"
#include "bench_image_protocols.h"
#include "bench_scrollback.h"
#include "bench_threading.h"
#include "bench_agent.h"
#include "bench_lua.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace bench {

/// Get current ISO 8601 timestamp.
static std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

/// Get OS name string.
static std::string getOsName() {
#ifdef _WIN32
    return "Windows";
#elif __APPLE__
    return "macOS";
#else
    return "Linux";
#endif
}

/// Get CPU name (best effort).
static std::string getCpuName() {
#ifdef _WIN32
    char cpu_name[256] = {};
    DWORD size = sizeof(cpu_name);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr,
                        reinterpret_cast<LPBYTE>(cpu_name), &size);
        RegCloseKey(hKey);
    }
    return cpu_name[0] ? cpu_name : "Unknown CPU";
#else
    return "Unknown CPU";
#endif
}

/// Get total RAM in GB (best effort).
static int getRamGb() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);
    return static_cast<int>(memInfo.ullTotalPhys / (1024ULL * 1024 * 1024));
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    return static_cast<int>((pages * page_size) / (1024L * 1024 * 1024));
#endif
}

/// Escape a string for JSON output.
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

/// Format results as JSON.
static std::string toJson(const std::vector<BenchmarkResult>& results) {
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);

    json << "{\n";
    json << "  \"version\": \"1.0\",\n";
    json << "  \"timestamp\": \"" << getTimestamp() << "\",\n";
    json << "  \"system\": {\n";
    json << "    \"os\": \"" << jsonEscape(getOsName()) << "\",\n";
    json << "    \"cpu\": \"" << jsonEscape(getCpuName()) << "\",\n";
    json << "    \"ram_gb\": " << getRamGb() << "\n";
    json << "  },\n";
    json << "  \"benchmarks\": [\n";

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        json << "    {\n";
        json << "      \"suite\": \"" << jsonEscape(r.suite) << "\",\n";
        json << "      \"name\": \"" << jsonEscape(r.name) << "\",\n";
        json << "      \"unit\": \"" << jsonEscape(r.unit) << "\",\n";
        json << "      \"iterations\": " << r.iterations << ",\n";
        json << "      \"mean\": " << r.mean << ",\n";
        json << "      \"median\": " << r.median << ",\n";
        json << "      \"min\": " << r.min << ",\n";
        json << "      \"max\": " << r.max << ",\n";
        json << "      \"stddev\": " << r.stddev << "\n";
        json << "    }";
        if (i + 1 < results.size()) json << ",";
        json << "\n";
    }

    json << "  ]\n";
    json << "}\n";
    return json.str();
}

/// Load previous results from a JSON file for comparison.
/// Returns a map from "suite/name" -> BenchmarkResult.
static std::map<std::string, BenchmarkResult> loadBaseline(const std::string& path) {
    std::map<std::string, BenchmarkResult> baseline;

    std::ifstream file(path);
    if (!file.is_open()) return baseline;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Simple JSON parser for our known format.
    // Find each benchmark entry between { } in the benchmarks array.
    size_t pos = content.find("\"benchmarks\"");
    if (pos == std::string::npos) return baseline;

    auto extractString = [&](const std::string& key, size_t start, size_t end) -> std::string {
        std::string search = "\"" + key + "\": \"";
        size_t p = content.find(search, start);
        if (p == std::string::npos || p >= end) return "";
        p += search.size();
        size_t pe = content.find('"', p);
        if (pe == std::string::npos || pe >= end) return "";
        return content.substr(p, pe - p);
    };

    auto extractDouble = [&](const std::string& key, size_t start, size_t end) -> double {
        std::string search = "\"" + key + "\": ";
        size_t p = content.find(search, start);
        if (p == std::string::npos || p >= end) return 0.0;
        p += search.size();
        return std::stod(content.substr(p));
    };

    auto extractInt = [&](const std::string& key, size_t start, size_t end) -> int {
        std::string search = "\"" + key + "\": ";
        size_t p = content.find(search, start);
        if (p == std::string::npos || p >= end) return 0;
        p += search.size();
        return std::stoi(content.substr(p));
    };

    // Find each benchmark object
    size_t search_from = pos;
    while (true) {
        size_t obj_start = content.find('{', search_from + 1);
        if (obj_start == std::string::npos) break;
        size_t obj_end = content.find('}', obj_start);
        if (obj_end == std::string::npos) break;

        std::string suite = extractString("suite", obj_start, obj_end);
        std::string name = extractString("name", obj_start, obj_end);
        if (suite.empty() || name.empty()) {
            search_from = obj_end;
            continue;
        }

        BenchmarkResult r;
        r.suite = suite;
        r.name = name;
        r.unit = extractString("unit", obj_start, obj_end);
        r.iterations = extractInt("iterations", obj_start, obj_end);
        r.mean = extractDouble("mean", obj_start, obj_end);
        r.median = extractDouble("median", obj_start, obj_end);
        r.min = extractDouble("min", obj_start, obj_end);
        r.max = extractDouble("max", obj_start, obj_end);
        r.stddev = extractDouble("stddev", obj_start, obj_end);

        baseline[suite + "/" + name] = r;
        search_from = obj_end;
    }

    return baseline;
}

/// Print results as a formatted table to stdout.
static void printTable(const std::vector<BenchmarkResult>& results,
                       const std::map<std::string, BenchmarkResult>& baseline) {
    std::string current_suite;
    bool has_baseline = !baseline.empty();

    for (const auto& r : results) {
        if (r.suite != current_suite) {
            current_suite = r.suite;
            std::cout << "\n";
            std::cout << "=== " << current_suite << " ===" << "\n";
            if (has_baseline) {
                std::cout << std::left << std::setw(40) << "Benchmark"
                          << std::right << std::setw(12) << "Mean"
                          << std::setw(12) << "Median"
                          << std::setw(10) << "StdDev"
                          << std::setw(8) << "Unit"
                          << std::setw(12) << "Delta"
                          << "\n";
            } else {
                std::cout << std::left << std::setw(40) << "Benchmark"
                          << std::right << std::setw(12) << "Mean"
                          << std::setw(12) << "Median"
                          << std::setw(10) << "StdDev"
                          << std::setw(8) << "Unit"
                          << "\n";
            }
            std::cout << std::string(has_baseline ? 94 : 82, '-') << "\n";
        }

        std::cout << std::left << std::setw(40) << r.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(12) << r.mean
                  << std::setw(12) << r.median
                  << std::setw(10) << r.stddev
                  << std::setw(8) << r.unit;

        if (has_baseline) {
            std::string key = r.suite + "/" + r.name;
            auto it = baseline.find(key);
            if (it != baseline.end() && it->second.mean > 0) {
                double pct = ((r.mean - it->second.mean) / it->second.mean) * 100.0;
                std::ostringstream delta;
                delta << std::fixed << std::setprecision(1)
                      << (pct >= 0 ? "+" : "") << pct << "%";
                std::cout << std::setw(12) << delta.str();
            } else {
                std::cout << std::setw(12) << "new";
            }
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS] [SUITE...]\n"
              << "\n"
              << "Suites: vt_parser, screen, font, cell_builder, e2e, image_protocols, scrollback, threading, agent, lua\n"
              << "If no suite is specified, all suites are run.\n"
              << "\n"
              << "Options:\n"
              << "  --warmup N        Warmup iterations (default: 3)\n"
              << "  --iterations N    Measurement iterations (default: 10)\n"
              << "  --json FILE       Write JSON results to FILE\n"
              << "  --compare FILE    Compare against baseline JSON file\n"
              << "  --quiet           Only output JSON (suppress table)\n"
              << "  --help            Show this help\n";
}

} // namespace bench

int main(int argc, char* argv[]) {
    int warmup = 3;
    int iterations = 10;
    std::string json_output;
    std::string baseline_path;
    bool quiet = false;
    std::set<std::string> suites;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--warmup" && i + 1 < argc) {
            warmup = std::stoi(argv[++i]);
        } else if (arg == "--iterations" && i + 1 < argc) {
            iterations = std::stoi(argv[++i]);
        } else if (arg == "--json" && i + 1 < argc) {
            json_output = argv[++i];
        } else if (arg == "--compare" && i + 1 < argc) {
            baseline_path = argv[++i];
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--help" || arg == "-h") {
            bench::printUsage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            suites.insert(arg);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            bench::printUsage(argv[0]);
            return 1;
        }
    }

    // Default: run all suites
    bool run_all = suites.empty();

    if (!quiet) {
        std::cout << "BreadTerminal Performance Benchmarks\n";
        std::cout << "Warmup: " << warmup << " iterations, Measure: " << iterations << " iterations\n";
        std::cout << "System: " << bench::getOsName() << ", " << bench::getCpuName()
                  << ", " << bench::getRamGb() << " GB RAM\n";
    }

    std::vector<bench::BenchmarkResult> all_results;

    // VT Parser benchmarks
    if (run_all || suites.count("vt_parser")) {
        bench::BenchmarkRunner runner("vt_parser", warmup, iterations);
        if (!quiet) std::cout << "\nRunning vt_parser benchmarks..." << std::flush;
        bench::runVtParserBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Screen benchmarks
    if (run_all || suites.count("screen")) {
        bench::BenchmarkRunner runner("screen", warmup, iterations);
        if (!quiet) std::cout << "Running screen benchmarks..." << std::flush;
        bench::runScreenBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Font benchmarks
    if (run_all || suites.count("font")) {
        bench::BenchmarkRunner runner("font", warmup, iterations);
        if (!quiet) std::cout << "Running font benchmarks..." << std::flush;
        bench::runFontBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Cell builder benchmarks
    if (run_all || suites.count("cell_builder")) {
        bench::BenchmarkRunner runner("cell_builder", warmup, iterations);
        if (!quiet) std::cout << "Running cell_builder benchmarks..." << std::flush;
        bench::runCellBuilderBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // End-to-end benchmarks
    if (run_all || suites.count("e2e")) {
        bench::BenchmarkRunner runner("e2e", warmup, iterations);
        if (!quiet) std::cout << "Running e2e benchmarks..." << std::flush;
        bench::runE2EBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Image protocol benchmarks
    if (run_all || suites.count("image_protocols")) {
        bench::BenchmarkRunner runner("image_protocols", warmup, iterations);
        if (!quiet) std::cout << "Running image_protocols benchmarks..." << std::flush;
        bench::runImageProtocolBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Scrollback benchmarks
    if (run_all || suites.count("scrollback")) {
        bench::BenchmarkRunner runner("scrollback", warmup, iterations);
        if (!quiet) std::cout << "Running scrollback benchmarks..." << std::flush;
        bench::runScrollbackBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Threading benchmarks
    if (run_all || suites.count("threading")) {
        bench::BenchmarkRunner runner("threading", warmup, iterations);
        if (!quiet) std::cout << "Running threading benchmarks..." << std::flush;
        bench::runThreadingBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Agent benchmarks
    if (run_all || suites.count("agent")) {
        bench::BenchmarkRunner runner("agent", warmup, iterations);
        if (!quiet) std::cout << "Running agent benchmarks..." << std::flush;
        bench::runAgentBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Lua engine benchmarks
    if (run_all || suites.count("lua")) {
        bench::BenchmarkRunner runner("lua", warmup, iterations);
        if (!quiet) std::cout << "Running lua benchmarks..." << std::flush;
        bench::runLuaBenchmarks(runner);
        if (!quiet) std::cout << " done.\n";
        for (const auto& r : runner.results())
            all_results.push_back(r);
    }

    // Load baseline for comparison
    std::map<std::string, bench::BenchmarkResult> baseline;
    if (!baseline_path.empty()) {
        baseline = bench::loadBaseline(baseline_path);
        if (!quiet && baseline.empty()) {
            std::cerr << "Warning: could not load baseline from " << baseline_path << "\n";
        }
    }

    // Print results table
    if (!quiet) {
        bench::printTable(all_results, baseline);
    }

    // Write JSON output
    if (!json_output.empty()) {
        std::string json = bench::toJson(all_results);
        if (json_output == "-") {
            std::cout << json;
        } else {
            std::ofstream file(json_output);
            if (file.is_open()) {
                file << json;
                if (!quiet) {
                    std::cout << "Results written to " << json_output << "\n";
                }
            } else {
                std::cerr << "Error: could not write to " << json_output << "\n";
                return 1;
            }
        }
    }

    return 0;
}
