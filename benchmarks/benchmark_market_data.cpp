#include "../engine/MarketData.hpp"
#include "BenchmarkUtils.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace hft::benchmarks;

namespace {

/**
 * @brief Runs throughput benchmark for a specific number of ticks.
 * Setup and output are kept strictly outside the timed loop.
 */
BenchmarkResult run_market_data_throughput(std::size_t total_ticks, uint32_t seed) {
    if (total_ticks == 0) {
        throw std::invalid_argument("Workload size must be strictly positive (> 0)");
    }

    // 1. Setup outside timed region
    MarketDataGenerator generator(100.0, total_ticks, seed);

    // 2. Timed loop measuring only generation
    const auto start_time = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < total_ticks; ++i) {
        MarketTick tick = generator.next_tick();
        do_not_optimize(tick);
    }
    const auto end_time = std::chrono::steady_clock::now();

    // 3. Calculation outside timed region
    const double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    const double throughput = elapsed_seconds > 0.0 ? (static_cast<double>(total_ticks) / elapsed_seconds) : 0.0;

    BenchmarkResult res;
    res.benchmark_name = "MarketDataGenerator";
    res.workload_type = "Throughput";
    res.workload_size = total_ticks;
    res.operations_count = total_ticks;
    res.trades_count = 0;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput;
    res.throughput_trades_sec = 0.0;
    res.seed = seed;
    res.notes = "Measures MarketDataGenerator::next_tick() sequential throughput";
    return res;
}

/**
 * @brief Measures batched effective per-operation latency distribution for MarketDataGenerator::next_tick().
 *
 * Each sample measures the wall-clock time to generate a batch of B ticks, divided by B.
 * By timing batches of B=1000 operations (~4.8 µs per batch on typical CPUs):
 * 1. The ~31 ns call overhead of steady_clock::now() is amortized across 1000 operations
 *    to ~0.03 ns/op (negligible).
 * 2. The total batch duration (~4.8 µs) spans >115 ticks of the 24 MHz hardware counter,
 *    eliminating the 41.67 ns timer quantization ceiling and yielding the true unquantized
 *    operation latency distribution without subtracting arbitrary estimated timer overhead.
 */
BenchmarkResult run_market_data_batched_latency(std::size_t batch_size,
                                                std::size_t batch_count,
                                                std::size_t warmup_batches,
                                                uint32_t seed) {
    if (batch_size == 0 || batch_count == 0) {
        throw std::invalid_argument("Batch size and batch count must be strictly positive (> 0)");
    }

    const std::size_t total_required = (warmup_batches + batch_count) * batch_size;
    MarketDataGenerator generator(100.0, total_required, seed);

    // 1. Warm-up iterations (untimed / discarded)
    for (std::size_t b = 0; b < warmup_batches; ++b) {
        for (std::size_t i = 0; i < batch_size; ++i) {
            MarketTick tick = generator.next_tick();
            do_not_optimize(tick);
        }
    }

    // 2. Measured batches
    std::vector<double> effective_samples_ns;
    effective_samples_ns.reserve(batch_count);

    const auto overall_start = std::chrono::steady_clock::now();
    for (std::size_t b = 0; b < batch_count; ++b) {
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < batch_size; ++i) {
            MarketTick tick = generator.next_tick();
            do_not_optimize(tick);
        }
        const auto t1 = std::chrono::steady_clock::now();

        const double batch_duration_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        const double effective_per_op_ns = batch_duration_ns / static_cast<double>(batch_size);
        effective_samples_ns.push_back(effective_per_op_ns);
    }
    const auto overall_end = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(overall_end - overall_start).count();
    const std::size_t total_operations = batch_size * batch_count;
    const double throughput = elapsed_seconds > 0.0 ? (static_cast<double>(total_operations) / elapsed_seconds) : 0.0;

    LatencyStats stats = calculate_latency_stats(effective_samples_ns);

    BenchmarkResult res;
    res.benchmark_name = "MarketDataGenerator";
    res.workload_type = "Batched Effective Latency";
    res.workload_size = total_operations;
    res.operations_count = total_operations;
    res.trades_count = 0;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput;
    res.throughput_trades_sec = 0.0;
    res.latency = stats;
    res.seed = seed;
    res.notes = "Batched effective latency (" + std::to_string(batch_count) + " batches of " +
                std::to_string(batch_size) + " ops); timer overhead amortized to <0.04 ns/op";
    return res;
}

/**
 * @brief Measures direct per-operation latency distribution for MarketDataGenerator::next_tick().
 *
 * NOTE: When an individual operation takes ~5 ns, isolated calls to steady_clock::now()
 * (~15-30 ns call overhead, 41.67 ns hardware quantum on Apple Silicon) cannot resolve
 * sub-quantum intervals. Samples cluster at 0 ns or 41.67 ns.
 */
BenchmarkResult run_market_data_direct_latency(std::size_t sample_count, std::size_t warmup_count, uint32_t seed) {
    if (sample_count == 0) {
        throw std::invalid_argument("Sample count must be strictly positive (> 0)");
    }

    const std::size_t total_required = warmup_count + sample_count;
    MarketDataGenerator generator(100.0, total_required, seed);

    // 1. Warm-up iterations (untimed / discarded)
    for (std::size_t i = 0; i < warmup_count; ++i) {
        MarketTick tick = generator.next_tick();
        do_not_optimize(tick);
    }

    // 2. Measured iterations
    std::vector<uint64_t> samples_ns;
    samples_ns.reserve(sample_count);

    const auto overall_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < sample_count; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        MarketTick tick = generator.next_tick();
        const auto t1 = std::chrono::steady_clock::now();

        do_not_optimize(tick);
        const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        samples_ns.push_back(static_cast<uint64_t>(duration_ns));
    }
    const auto overall_end = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(overall_end - overall_start).count();
    const double throughput = elapsed_seconds > 0.0 ? (static_cast<double>(sample_count) / elapsed_seconds) : 0.0;

    LatencyStats stats = calculate_latency_stats(samples_ns);

    BenchmarkResult res;
    res.benchmark_name = "MarketDataGenerator";
    res.workload_type = "Direct Single-Tick Latency (Discretized)";
    res.workload_size = sample_count;
    res.operations_count = sample_count;
    res.trades_count = 0;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput;
    res.throughput_trades_sec = 0.0;
    res.latency = stats;
    res.seed = seed;
    res.notes = "Direct single-tick latency (unbatched); discrete distribution subject to ~42 ns timer tick and ~31 ns call overhead";
    return res;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --ticks <N>            Run throughput benchmark for specific tick count\n"
              << "  --seed <N>             Random seed for reproducibility (default: 42)\n"
              << "  --latency-samples <N>  Number of direct latency samples to collect (default: 100000)\n"
              << "  --batch-size <B>       Batch size for effective latency measurement (default: 1000)\n"
              << "  --batch-count <N>      Number of batches for effective latency (default: 1000)\n"
              << "  --repetitions <R>      Number of benchmark repetitions to evaluate variability (default: 1)\n"
              << "  --warmup <N>           Number of warm-up iterations (default: 10000)\n"
              << "  --output <path>        Path to output CSV file (default: benchmark_results/market_data_benchmark.csv)\n"
              << "  --no-csv               Do not write results to CSV file\n"
              << "  --help, -h             Show this help message\n\n"
              << "Default behavior without --ticks: runs standard workloads (100k, 1M, 5M ticks)\n"
              << "followed by batched effective latency and direct single-op latency distributions.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::size_t> workloads;
    uint32_t seed = 42;
    std::size_t latency_samples = 100000;
    std::size_t batch_size = 1000;
    std::size_t batch_count = 1000;
    std::size_t repetitions = 1;
    std::size_t warmup_count = 10000;
    std::string csv_path = "benchmark_results/market_data_benchmark.csv";
    bool write_csv = true;
    bool custom_workload = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--ticks" && i + 1 < argc) {
            try {
                long long val = std::stoll(argv[++i]);
                if (val <= 0) {
                    std::cerr << "[Error] --ticks must be strictly positive (> 0); got: " << val << "\n";
                    return 1;
                }
                workloads.push_back(static_cast<std::size_t>(val));
                custom_workload = true;
            } catch (const std::exception&) {
                std::cerr << "[Error] Invalid value for --ticks: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--seed" && i + 1 < argc) {
            try {
                seed = static_cast<uint32_t>(std::stoul(argv[++i]));
            } catch (const std::exception&) {
                std::cerr << "[Error] Invalid value for --seed: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--latency-samples" && i + 1 < argc) {
            try {
                long long val = std::stoll(argv[++i]);
                if (val <= 0) {
                    std::cerr << "[Error] --latency-samples must be strictly positive (> 0); got: " << val << "\n";
                    return 1;
                }
                latency_samples = static_cast<std::size_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[Error] Invalid value for --latency-samples: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--batch-size" && i + 1 < argc) {
            try {
                long long val = std::stoll(argv[++i]);
                if (val <= 0) {
                    std::cerr << "[Error] --batch-size must be strictly positive (> 0); got: " << val << "\n";
                    return 1;
                }
                batch_size = static_cast<std::size_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[Error] Invalid value for --batch-size: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--batch-count" && i + 1 < argc) {
            try {
                long long val = std::stoll(argv[++i]);
                if (val <= 0) {
                    std::cerr << "[Error] --batch-count must be strictly positive (> 0); got: " << val << "\n";
                    return 1;
                }
                batch_count = static_cast<std::size_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[Error] Invalid value for --batch-count: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--repetitions" && i + 1 < argc) {
            try {
                long long val = std::stoll(argv[++i]);
                if (val <= 0) {
                    std::cerr << "[Error] --repetitions must be strictly positive (> 0); got: " << val << "\n";
                    return 1;
                }
                repetitions = static_cast<std::size_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[Error] Invalid value for --repetitions: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--warmup" && i + 1 < argc) {
            try {
                long long val = std::stoll(argv[++i]);
                if (val < 0) {
                    std::cerr << "[Error] --warmup must be non-negative (>= 0); got: " << val << "\n";
                    return 1;
                }
                warmup_count = static_cast<std::size_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[Error] Invalid value for --warmup: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--output" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "--no-csv") {
            write_csv = false;
        } else {
            std::cerr << "[Error] Unrecognized argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Default workloads if none specified
    if (!custom_workload) {
        workloads = {100000, 1000000, 5000000};
    }

    std::cout << "\n============================================================\n";
    std::cout << "      HFT Simulation Engine: Market Data Benchmark         \n";
    std::cout << "============================================================\n";
    std::cout << "  Random Seed        : " << seed << "\n";
    std::cout << "  Repetitions        : " << repetitions << "\n";
    std::cout << "  Throughput Sizes   : ";
    for (std::size_t idx = 0; idx < workloads.size(); ++idx) {
        std::cout << workloads[idx] << (idx + 1 < workloads.size() ? ", " : "");
    }
    std::cout << "\n";
    std::cout << "  Batched Latency    : " << batch_count << " batches of " << batch_size << " ticks\n";
    std::cout << "  Direct Latency     : " << latency_samples << " samples (Warm-up: " << warmup_count << ")\n";
    if (write_csv) {
        std::cout << "  CSV Output         : " << csv_path << "\n";
    }
    std::cout << "============================================================\n\n";

    // Perform clock audit probe
    ClockCharacteristics clock_info = probe_clock_characteristics();
    print_clock_characteristics(clock_info);

    std::vector<BenchmarkResult> all_results;

    for (std::size_t rep = 1; rep <= repetitions; ++rep) {
        if (repetitions > 1) {
            std::cout << ">>> Running Repetition " << rep << " of " << repetitions << " (Seed: " << seed << ")\n\n";
        }

        // 1. Throughput Benchmarks
        for (std::size_t size : workloads) {
            std::cout << "Running throughput benchmark for " << size << " ticks...\n";
            try {
                BenchmarkResult res = run_market_data_throughput(size, seed);
                if (repetitions > 1) {
                    res.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
                }
                print_benchmark_result(res);
                all_results.push_back(res);
            } catch (const std::exception& ex) {
                std::cerr << "[Error] Failed benchmark for " << size << " ticks: " << ex.what() << "\n";
                return 1;
            }
        }

        // 2. Batched Latency Measurement Benchmark
        std::cout << "Running batched effective latency benchmark (" << batch_count << " batches of " << batch_size << " ticks)...\n";
        try {
            std::size_t warmup_batches = (warmup_count / batch_size > 0) ? (warmup_count / batch_size) : 10;
            BenchmarkResult batch_res = run_market_data_batched_latency(batch_size, batch_count, warmup_batches, seed);
            if (repetitions > 1) {
                batch_res.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
            }
            print_benchmark_result(batch_res);
            all_results.push_back(batch_res);
        } catch (const std::exception& ex) {
            std::cerr << "[Error] Failed batched latency benchmark: " << ex.what() << "\n";
            return 1;
        }

        // 3. Direct Latency Measurement Benchmark
        std::cout << "Running direct single-operation latency benchmark (" << latency_samples << " samples)...\n";
        try {
            BenchmarkResult lat_res = run_market_data_direct_latency(latency_samples, warmup_count, seed);
            if (repetitions > 1) {
                lat_res.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
            }
            print_benchmark_result(lat_res);
            all_results.push_back(lat_res);
        } catch (const std::exception& ex) {
            std::cerr << "[Error] Failed latency benchmark: " << ex.what() << "\n";
            return 1;
        }
    }

    // 4. Write CSV output
    if (write_csv) {
        try {
            save_results_to_csv(csv_path, all_results);
            std::cout << "[Success] Benchmark results saved to " << csv_path << "\n\n";
        } catch (const std::exception& ex) {
            std::cerr << "[Error] Failed to write CSV file: " << ex.what() << "\n";
            return 1;
        }
    }

    return 0;
}
