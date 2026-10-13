#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hft::benchmarks {

/**
 * @brief Prevents the compiler from optimizing away expressions whose results
 * are not otherwise read, without introducing runtime side-effects.
 */
template <typename T>
inline void do_not_optimize(const T& value) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "g"(value) : "memory");
#else
    volatile const void* p = &value;
    (void)p;
#endif
}

/**
 * @brief System timer characteristics determined at runtime.
 */
struct ClockCharacteristics {
    bool is_steady{false};
    int64_t period_num{1};
    int64_t period_den{1000000000};
    double call_overhead_ns{0.0};
    double resolution_quantum_ns{0.0};
};

/**
 * @brief Empirically probes the resolution and call overhead of std::chrono::steady_clock.
 *
 * On modern POSIX systems, std::chrono::steady_clock often queries hardware registers
 * (e.g. ARM64 CNTVCT_EL0 @ 24 MHz on Apple Silicon, giving a 41.67 ns tick resolution).
 * Back-to-back calls also incur an instruction overhead (~15-30 ns).
 *
 * @param probe_iterations Number of probe iterations.
 */
inline ClockCharacteristics probe_clock_characteristics(std::size_t probe_iterations = 100000) {
    ClockCharacteristics info;
    info.is_steady = std::chrono::steady_clock::is_steady;
    info.period_num = std::chrono::steady_clock::period::num;
    info.period_den = std::chrono::steady_clock::period::den;

    // 1. Measure mean call overhead of steady_clock::now()
    const auto t_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < probe_iterations; ++i) {
        auto t = std::chrono::steady_clock::now();
        do_not_optimize(t);
    }
    const auto t_end = std::chrono::steady_clock::now();
    const double elapsed_ns = std::chrono::duration<double, std::nano>(t_end - t_start).count();
    info.call_overhead_ns = elapsed_ns / static_cast<double>(probe_iterations);

    // 2. Measure minimum non-zero quantum between consecutive calls
    double min_nonzero_delta_ns = 1e9;
    for (std::size_t i = 0; i < probe_iterations; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        auto t1 = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        if (diff > 0 && static_cast<double>(diff) < min_nonzero_delta_ns) {
            min_nonzero_delta_ns = static_cast<double>(diff);
        }
    }
    info.resolution_quantum_ns = (min_nonzero_delta_ns < 1e9) ? min_nonzero_delta_ns : 1.0;
    return info;
}

/**
 * @brief Prints diagnosed clock characteristics to std::cout.
 */
inline void print_clock_characteristics(const ClockCharacteristics& info) {
    std::cout << "------------------------------------------------------------\n";
    std::cout << " System Clock Audit (std::chrono::steady_clock)\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "  Clock Is Steady      : " << (info.is_steady ? "true" : "false") << "\n";
    std::cout << "  Period Ratio         : " << info.period_num << " / " << info.period_den << " s\n";
    std::cout << "  Call Overhead        : ~" << std::fixed << std::setprecision(2) << info.call_overhead_ns << " ns per call\n";
    std::cout << "  Resolution Quantum   : ~" << info.resolution_quantum_ns << " ns (hardware timer tick)\n";
    std::cout << "------------------------------------------------------------\n\n";
}

/**
 * @brief Distribution metrics for operation latency samples (measured in nanoseconds).
 */
struct LatencyStats {
    std::size_t sample_count{0};
    double min_ns{0.0};
    double max_ns{0.0};
    double mean_ns{0.0};
    double median_ns{0.0}; ///< 50th percentile (p50)
    double p95_ns{0.0};    ///< 95th percentile (p95)
    double p99_ns{0.0};    ///< 99th percentile (p99)
};

/**
 * @brief Calculates latency summary statistics from duration samples.
 *
 * Supports both integer nanosecond samples and floating-point effective nanoseconds
 * derived from amortized batched timings.
 *
 * Percentile Convention:
 * - Median (p50):
 *     If N is odd, middle element samples[N / 2].
 *     If N is even, average of middle two elements: (samples[N/2 - 1] + samples[N/2]) / 2.0.
 * - p95 and p99:
 *     Calculated using the standard NIST / Wikipedia Nearest Rank method:
 *       rank = ceil(P / 100 * N)
 *       zero_based_index = min(rank - 1, N - 1)
 *
 * @tparam T Numeric type of sample (e.g. uint64_t, int64_t, double).
 * @param samples Vector of latency samples. Will be sorted in-place.
 * @throws std::invalid_argument if samples is empty.
 */
template <typename T>
inline LatencyStats calculate_latency_stats(std::vector<T>& samples) {
    if (samples.empty()) {
        throw std::invalid_argument("Latency sample set cannot be empty");
    }

    std::sort(samples.begin(), samples.end());

    const std::size_t n = samples.size();
    LatencyStats stats;
    stats.sample_count = n;
    stats.min_ns = static_cast<double>(samples.front());
    stats.max_ns = static_cast<double>(samples.back());

    double sum = 0.0;
    for (const auto& sample : samples) {
        sum += static_cast<double>(sample);
    }
    stats.mean_ns = sum / static_cast<double>(n);

    // Median (p50)
    if (n % 2 == 1) {
        stats.median_ns = static_cast<double>(samples[n / 2]);
    } else {
        stats.median_ns = static_cast<double>(samples[(n / 2) - 1] + samples[n / 2]) / 2.0;
    }

    // p95 and p99 percentiles using standard nearest-rank indexing
    auto percentile_index = [n](double p) -> std::size_t {
        if (n == 1) return 0;
        std::size_t idx = static_cast<std::size_t>(std::ceil(p * static_cast<double>(n))) - 1;
        return std::min(idx, n - 1);
    };

    stats.p95_ns = static_cast<double>(samples[percentile_index(0.95)]);
    stats.p99_ns = static_cast<double>(samples[percentile_index(0.99)]);

    return stats;
}

/**
 * @brief Container representing the result of a single benchmark run.
 */
struct BenchmarkResult {
    std::string benchmark_name;
    std::string workload_type;
    std::size_t workload_size{0};
    std::size_t operations_count{0};
    std::size_t trades_count{0};
    double elapsed_seconds{0.0};
    double throughput_ops_sec{0.0};
    double throughput_trades_sec{0.0};
    std::optional<LatencyStats> latency{std::nullopt};
    std::optional<uint32_t> seed{std::nullopt};
    std::string notes;
};

/**
 * @brief Escapes a string field for inclusion in a standard RFC 4180 CSV document.
 */
inline std::string escape_csv_field(const std::string& field) {
    bool needs_quoting = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quoting = true;
            break;
        }
    }

    if (!needs_quoting) {
        return field;
    }

    std::string escaped = "\"";
    for (char c : field) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += "\"";
    return escaped;
}

/**
 * @brief Formats CSV header string.
 */
inline std::string get_csv_header() {
    return "benchmark_name,workload_type,workload_size,operations_count,trades_count,"
           "elapsed_seconds,throughput_ops_per_sec,throughput_trades_per_sec,"
           "median_latency_ns,p95_latency_ns,p99_latency_ns,seed,notes";
}

/**
 * @brief Formats a BenchmarkResult into a CSV row.
 */
inline std::string format_csv_row(const BenchmarkResult& res) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6);

    ss << escape_csv_field(res.benchmark_name) << ","
       << escape_csv_field(res.workload_type) << ","
       << res.workload_size << ","
       << res.operations_count << ","
       << res.trades_count << ","
       << res.elapsed_seconds << ","
       << std::setprecision(2) << res.throughput_ops_sec << ","
       << res.throughput_trades_sec << ",";

    if (res.latency.has_value()) {
        ss << res.latency->median_ns << ","
           << res.latency->p95_ns << ","
           << res.latency->p99_ns << ",";
    } else {
        ss << ",,,";
    }

    if (res.seed.has_value()) {
        ss << res.seed.value() << ",";
    } else {
        ss << ",";
    }

    ss << escape_csv_field(res.notes);
    return ss.str();
}

/**
 * @brief Saves a batch of benchmark results to a CSV file.
 * Creates any missing parent directories automatically.
 *
 * @param filepath Path to the output CSV file.
 * @param results Collection of benchmark records.
 * @param append If true, appends to an existing file without repeating header.
 */
inline void save_results_to_csv(const std::string& filepath,
                                const std::vector<BenchmarkResult>& results,
                                bool append = false) {
    std::filesystem::path p(filepath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    bool file_exists = std::filesystem::exists(p) && (std::filesystem::file_size(p) > 0);
    std::ofstream ofs(filepath, append ? std::ios::app : std::ios::trunc);
    if (!ofs.is_open()) {
        throw std::runtime_error("Failed to open benchmark CSV file for writing: " + filepath);
    }

    if (!append || !file_exists) {
        ofs << get_csv_header() << "\n";
    }

    for (const auto& res : results) {
        ofs << format_csv_row(res) << "\n";
    }
}

/**
 * @brief Pretty prints a single BenchmarkResult to standard out.
 */
inline void print_benchmark_result(const BenchmarkResult& res) {
    std::cout << "------------------------------------------------------------\n";
    std::cout << " Benchmark: " << res.benchmark_name << " [" << res.workload_type << "]\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "  Operations Processed : " << res.operations_count << "\n";
    if (res.trades_count > 0) {
        std::cout << "  Trades Generated     : " << res.trades_count << "\n";
    }
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  Elapsed Wall Time    : " << res.elapsed_seconds << " s\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Throughput           : " << res.throughput_ops_sec << " ops/sec\n";
    if (res.trades_count > 0 && res.throughput_trades_sec > 0.0) {
        std::cout << "  Trade Rate           : " << res.throughput_trades_sec << " trades/sec\n";
    }

    if (res.latency.has_value()) {
        const auto& l = res.latency.value();
        std::cout << "  Latency (Samples: " << l.sample_count << "):\n";
        std::cout << "    Min    : " << l.min_ns << " ns (" << (l.min_ns / 1000.0) << " µs)\n";
        std::cout << "    Mean   : " << l.mean_ns << " ns (" << (l.mean_ns / 1000.0) << " µs)\n";
        std::cout << "    Median : " << l.median_ns << " ns (" << (l.median_ns / 1000.0) << " µs)\n";
        std::cout << "    p95    : " << l.p95_ns << " ns (" << (l.p95_ns / 1000.0) << " µs)\n";
        std::cout << "    p99    : " << l.p99_ns << " ns (" << (l.p99_ns / 1000.0) << " µs)\n";
        std::cout << "    Max    : " << l.max_ns << " ns (" << (l.max_ns / 1000.0) << " µs)\n";
    }

    if (res.seed.has_value()) {
        std::cout << "  Random Seed          : " << res.seed.value() << "\n";
    }
    if (!res.notes.empty()) {
        std::cout << "  Notes                : " << res.notes << "\n";
    }
    std::cout << "------------------------------------------------------------\n\n";
}

} // namespace hft::benchmarks
