#include "../engine/ExecutionEngine.hpp"
#include "../engine/Order.hpp"
#include "BenchmarkUtils.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace hft::benchmarks;

namespace {

/**
 * @brief Workload 1: Measures throughput when limit orders rest in the book without matching.
 * Bids are placed at prices strictly below asks so no crossing occurs.
 */
BenchmarkResult run_resting_orders_throughput(std::size_t num_orders) {
    if (num_orders == 0) {
        throw std::invalid_argument("Number of orders must be strictly positive (> 0)");
    }

    ExecutionEngine engine;
    std::size_t orders_submitted = 0;
    std::size_t trades_generated = 0;
    OrderId next_id = 1;

    // Timed region: pure order submission and book insertion
    const auto start_time = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < num_orders; ++i) {
        OrderSide side = (i % 2 == 0) ? OrderSide::Buy : OrderSide::Sell;
        // Bids: 90.00 - offset; Asks: 110.00 + offset (never cross)
        double price = (side == OrderSide::Buy)
            ? (90.0 - static_cast<double>(i % 500) * 0.01)
            : (110.0 + static_cast<double>(i % 500) * 0.01);

        Order order = Order::create_limit(next_id++, side, 10, price);
        ExecutionResult res = engine.submit_order(order);

        do_not_optimize(res);
        ++orders_submitted;
        trades_generated += res.trades.size();
    }
    const auto end_time = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    const double throughput_ops = elapsed_seconds > 0.0 ? (static_cast<double>(orders_submitted) / elapsed_seconds) : 0.0;

    BenchmarkResult res;
    res.benchmark_name = "ExecutionEngine";
    res.workload_type = "Resting Orders (No Match)";
    res.workload_size = num_orders;
    res.operations_count = orders_submitted;
    res.trades_count = trades_generated;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput_ops;
    res.throughput_trades_sec = 0.0;
    res.notes = "Alternating non-crossing limit bids ($90-offset) and asks ($110+offset); 0 trades";
    return res;
}

/**
 * @brief Workload 2: Measures throughput when incoming orders match and generate trades.
 * For each pair: a resting limit sell order is submitted, followed by a matching buy order.
 */
BenchmarkResult run_matching_orders_throughput(std::size_t num_pairs) {
    if (num_pairs == 0) {
        throw std::invalid_argument("Number of pairs must be strictly positive (> 0)");
    }

    ExecutionEngine engine;
    std::size_t orders_submitted = 0;
    std::size_t trades_generated = 0;
    OrderId next_id = 1;

    const auto start_time = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < num_pairs; ++i) {
        // 1. Resting sell order @ $100.00
        Order sell_order = Order::create_limit(next_id++, OrderSide::Sell, 10, 100.0);
        ExecutionResult res_sell = engine.submit_order(sell_order);
        do_not_optimize(res_sell);
        ++orders_submitted;
        trades_generated += res_sell.trades.size();

        // 2. Matching buy order @ $100.00 (crosses and executes immediately)
        Order buy_order = Order::create_limit(next_id++, OrderSide::Buy, 10, 100.0);
        ExecutionResult res_buy = engine.submit_order(buy_order);
        do_not_optimize(res_buy);
        ++orders_submitted;
        trades_generated += res_buy.trades.size();
    }
    const auto end_time = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    const double throughput_ops = elapsed_seconds > 0.0 ? (static_cast<double>(orders_submitted) / elapsed_seconds) : 0.0;
    const double throughput_trades = elapsed_seconds > 0.0 ? (static_cast<double>(trades_generated) / elapsed_seconds) : 0.0;

    BenchmarkResult res;
    res.benchmark_name = "ExecutionEngine";
    res.workload_type = "Matching Orders (Trade Execution)";
    res.workload_size = orders_submitted;
    res.operations_count = orders_submitted;
    res.trades_count = trades_generated;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput_ops;
    res.throughput_trades_sec = throughput_trades;
    res.notes = "Matched pairs (" + std::to_string(num_pairs) + " resting sells + " +
                std::to_string(num_pairs) + " aggressive buys); " +
                std::to_string(trades_generated) + " trades generated";
    return res;
}

/**
 * @brief Measures batched effective per-order latency distribution for resting limit orders.
 *
 * Submits batches of B non-crossing limit orders and records (batch_duration / B).
 * Amortizes steady_clock call overhead to <0.35 ns/order and eliminates timer quantization error.
 */
BenchmarkResult run_resting_order_batched_latency(std::size_t batch_size,
                                                  std::size_t batch_count,
                                                  std::size_t warmup_batches) {
    if (batch_size == 0 || batch_count == 0) {
        throw std::invalid_argument("Batch size and batch count must be strictly positive (> 0)");
    }

    ExecutionEngine engine;
    OrderId next_id = 1;

    // 1. Warm-up batches
    for (std::size_t b = 0; b < warmup_batches; ++b) {
        for (std::size_t i = 0; i < batch_size; ++i) {
            OrderSide side = (i % 2 == 0) ? OrderSide::Buy : OrderSide::Sell;
            double price = (side == OrderSide::Buy)
                ? (90.0 - static_cast<double>(i % 500) * 0.01)
                : (110.0 + static_cast<double>(i % 500) * 0.01);
            Order order = Order::create_limit(next_id++, side, 10, price);
            ExecutionResult res = engine.submit_order(order);
            do_not_optimize(res);
        }
    }

    // 2. Measured batches
    std::vector<double> effective_samples_ns;
    effective_samples_ns.reserve(batch_count);
    std::size_t orders_submitted = 0;

    const auto overall_start = std::chrono::steady_clock::now();
    for (std::size_t b = 0; b < batch_count; ++b) {
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < batch_size; ++i) {
            OrderSide side = (i % 2 == 0) ? OrderSide::Buy : OrderSide::Sell;
            double price = (side == OrderSide::Buy)
                ? (80.0 - static_cast<double>(i % 500) * 0.01)
                : (120.0 + static_cast<double>(i % 500) * 0.01);
            Order order = Order::create_limit(next_id++, side, 10, price);
            ExecutionResult res = engine.submit_order(order);
            do_not_optimize(res);
            ++orders_submitted;
        }
        const auto t1 = std::chrono::steady_clock::now();

        const double batch_duration_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        const double effective_per_order_ns = batch_duration_ns / static_cast<double>(batch_size);
        effective_samples_ns.push_back(effective_per_order_ns);
    }
    const auto overall_end = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(overall_end - overall_start).count();
    const double throughput = elapsed_seconds > 0.0 ? (static_cast<double>(orders_submitted) / elapsed_seconds) : 0.0;

    LatencyStats stats = calculate_latency_stats(effective_samples_ns);

    BenchmarkResult res;
    res.benchmark_name = "ExecutionEngine";
    res.workload_type = "Batched Resting Order Latency";
    res.workload_size = orders_submitted;
    res.operations_count = orders_submitted;
    res.trades_count = 0;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput;
    res.throughput_trades_sec = 0.0;
    res.latency = stats;
    res.notes = "Batched resting order latency (" + std::to_string(batch_count) + " batches of " +
                std::to_string(batch_size) + " orders); timer overhead amortized to <0.35 ns/op";
    return res;
}

/**
 * @brief Measures batched effective per-match latency distribution for aggressive matching orders.
 *
 * For each batch of B trades:
 * 1. B resting limit sell orders are submitted outside the timed region.
 * 2. In the timed region, B aggressive market buy orders are submitted, each matching a resting sell.
 * Records (batch_duration / B) as the effective trade execution latency.
 */
BenchmarkResult run_matching_order_batched_latency(std::size_t batch_size,
                                                   std::size_t batch_count,
                                                   std::size_t warmup_batches) {
    if (batch_size == 0 || batch_count == 0) {
        throw std::invalid_argument("Batch size and batch count must be strictly positive (> 0)");
    }

    ExecutionEngine engine;
    OrderId next_id = 1;

    // 1. Warm-up
    for (std::size_t b = 0; b < warmup_batches; ++b) {
        for (std::size_t i = 0; i < batch_size; ++i) {
            Order resting = Order::create_limit(next_id++, OrderSide::Sell, 10, 100.0);
            auto r0 = engine.submit_order(resting);
            do_not_optimize(r0);
        }
        for (std::size_t i = 0; i < batch_size; ++i) {
            Order matching = Order::create_market(next_id++, OrderSide::Buy, 10);
            auto r1 = engine.submit_order(matching);
            do_not_optimize(r1);
        }
    }

    // 2. Measured batches
    std::vector<double> effective_samples_ns;
    effective_samples_ns.reserve(batch_count);
    std::size_t trades_generated = 0;

    const auto overall_start = std::chrono::steady_clock::now();
    for (std::size_t b = 0; b < batch_count; ++b) {
        // Prepare resting orders outside timed block
        for (std::size_t i = 0; i < batch_size; ++i) {
            Order resting = Order::create_limit(next_id++, OrderSide::Sell, 10, 100.0);
            ExecutionResult res_rest = engine.submit_order(resting);
            do_not_optimize(res_rest);
        }

        // Timed region: exclusively the execution of B matching market orders
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < batch_size; ++i) {
            Order matching = Order::create_market(next_id++, OrderSide::Buy, 10);
            ExecutionResult res_match = engine.submit_order(matching);
            do_not_optimize(res_match);
            trades_generated += res_match.trades.size();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const double batch_duration_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        const double effective_per_match_ns = batch_duration_ns / static_cast<double>(batch_size);
        effective_samples_ns.push_back(effective_per_match_ns);
    }
    const auto overall_end = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(overall_end - overall_start).count();
    const std::size_t total_matches = batch_size * batch_count;
    const double throughput_ops = elapsed_seconds > 0.0 ? (static_cast<double>(total_matches) / elapsed_seconds) : 0.0;
    const double throughput_trades = elapsed_seconds > 0.0 ? (static_cast<double>(trades_generated) / elapsed_seconds) : 0.0;

    LatencyStats stats = calculate_latency_stats(effective_samples_ns);

    BenchmarkResult res;
    res.benchmark_name = "ExecutionEngine";
    res.workload_type = "Batched Matching Order Latency";
    res.workload_size = total_matches;
    res.operations_count = total_matches;
    res.trades_count = trades_generated;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput_ops;
    res.throughput_trades_sec = throughput_trades;
    res.latency = stats;
    res.notes = "Batched matching order latency (" + std::to_string(batch_count) + " batches of " +
                std::to_string(batch_size) + " trades); timer overhead amortized to <0.35 ns/match";
    return res;
}

/**
 * @brief Measures direct per-operation latency distribution for resting limit order submission.
 * Timed interval begins immediately before engine.submit_order() and ends immediately after it returns.
 * Subject to timer quantization (~42 ns on Apple Silicon).
 */
BenchmarkResult run_resting_order_direct_latency(std::size_t sample_count, std::size_t warmup_count) {
    if (sample_count == 0) {
        throw std::invalid_argument("Sample count must be strictly positive (> 0)");
    }

    ExecutionEngine engine;
    OrderId next_id = 1;

    // 1. Warm-up
    for (std::size_t i = 0; i < warmup_count; ++i) {
        Order order = Order::create_limit(next_id++, OrderSide::Buy, 10, 90.0 - static_cast<double>(i % 500) * 0.01);
        ExecutionResult res = engine.submit_order(order);
        do_not_optimize(res);
    }

    // 2. Measured samples
    std::vector<uint64_t> samples_ns;
    samples_ns.reserve(sample_count);

    const auto overall_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < sample_count; ++i) {
        Order order = Order::create_limit(next_id++, OrderSide::Buy, 10, 80.0 - static_cast<double>(i % 500) * 0.01);

        const auto t0 = std::chrono::steady_clock::now();
        ExecutionResult res = engine.submit_order(order);
        const auto t1 = std::chrono::steady_clock::now();

        do_not_optimize(res);
        const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        samples_ns.push_back(static_cast<uint64_t>(duration_ns));
    }
    const auto overall_end = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(overall_end - overall_start).count();
    const double throughput = elapsed_seconds > 0.0 ? (static_cast<double>(sample_count) / elapsed_seconds) : 0.0;

    LatencyStats stats = calculate_latency_stats(samples_ns);

    BenchmarkResult res;
    res.benchmark_name = "ExecutionEngine";
    res.workload_type = "Direct Resting Order Latency (Discretized)";
    res.workload_size = sample_count;
    res.operations_count = sample_count;
    res.trades_count = 0;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput;
    res.throughput_trades_sec = 0.0;
    res.latency = stats;
    res.notes = "Direct resting order latency (unbatched); discrete distribution subject to ~42 ns timer tick and ~31 ns call overhead";
    return res;
}

/**
 * @brief Measures direct per-operation latency distribution for aggressive matching order execution.
 * Outside the timed interval: rests an opposing order.
 * Inside the timed interval: submits the matching aggressive order and records execution time.
 * Subject to timer quantization (~42 ns on Apple Silicon).
 */
BenchmarkResult run_matching_order_direct_latency(std::size_t sample_count, std::size_t warmup_count) {
    if (sample_count == 0) {
        throw std::invalid_argument("Sample count must be strictly positive (> 0)");
    }

    ExecutionEngine engine;
    OrderId next_id = 1;

    // 1. Warm-up
    for (std::size_t i = 0; i < warmup_count; ++i) {
        Order resting = Order::create_limit(next_id++, OrderSide::Sell, 10, 100.0);
        auto r0 = engine.submit_order(resting);
        do_not_optimize(r0);

        Order matching = Order::create_market(next_id++, OrderSide::Buy, 10);
        auto r1 = engine.submit_order(matching);
        do_not_optimize(r1);
    }

    // 2. Measured samples
    std::vector<uint64_t> samples_ns;
    samples_ns.reserve(sample_count);
    std::size_t trades_count = 0;

    const auto overall_start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < sample_count; ++i) {
        // Place resting order outside the timed block
        Order resting = Order::create_limit(next_id++, OrderSide::Sell, 10, 100.0);
        ExecutionResult res_rest = engine.submit_order(resting);
        do_not_optimize(res_rest);

        Order matching = Order::create_market(next_id++, OrderSide::Buy, 10);

        // Timed interval: exclusively the aggressive matching execution
        const auto t0 = std::chrono::steady_clock::now();
        ExecutionResult res_match = engine.submit_order(matching);
        const auto t1 = std::chrono::steady_clock::now();

        do_not_optimize(res_match);
        trades_count += res_match.trades.size();

        const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        samples_ns.push_back(static_cast<uint64_t>(duration_ns));
    }
    const auto overall_end = std::chrono::steady_clock::now();

    const double elapsed_seconds = std::chrono::duration<double>(overall_end - overall_start).count();
    const double throughput = elapsed_seconds > 0.0 ? (static_cast<double>(sample_count) / elapsed_seconds) : 0.0;
    const double trade_rate = elapsed_seconds > 0.0 ? (static_cast<double>(trades_count) / elapsed_seconds) : 0.0;

    LatencyStats stats = calculate_latency_stats(samples_ns);

    BenchmarkResult res;
    res.benchmark_name = "ExecutionEngine";
    res.workload_type = "Direct Matching Order Latency (Discretized)";
    res.workload_size = sample_count;
    res.operations_count = sample_count;
    res.trades_count = trades_count;
    res.elapsed_seconds = elapsed_seconds;
    res.throughput_ops_sec = throughput;
    res.throughput_trades_sec = trade_rate;
    res.latency = stats;
    res.notes = "Direct matching order latency (unbatched); discrete distribution subject to ~42 ns timer tick and ~31 ns call overhead";
    return res;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --ops <N>              Run throughput benchmark for specific operation count\n"
              << "  --latency-samples <N>  Number of direct latency samples to collect (default: 100000)\n"
              << "  --batch-size <B>       Batch size for effective latency measurement (default: 100)\n"
              << "  --batch-count <N>      Number of batches for effective latency (default: 1000)\n"
              << "  --repetitions <R>      Number of benchmark repetitions to evaluate variability (default: 1)\n"
              << "  --warmup <N>           Number of warm-up iterations (default: 10000)\n"
              << "  --output <path>        Path to output CSV file (default: benchmark_results/order_execution_benchmark.csv)\n"
              << "  --no-csv               Do not write results to CSV file\n"
              << "  --help, -h             Show this help message\n\n"
              << "Default behavior without --ops: runs standard suite (10k, 100k, 500k ops)\n"
              << "for both resting and matching workloads, followed by batched effective and direct latency distributions.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::size_t> workloads;
    std::size_t latency_samples = 100000;
    std::size_t batch_size = 100;
    std::size_t batch_count = 1000;
    std::size_t repetitions = 1;
    std::size_t warmup_count = 10000;
    std::string csv_path = "benchmark_results/order_execution_benchmark.csv";
    bool write_csv = true;
    bool custom_workload = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--ops" && i + 1 < argc) {
            try {
                long long val = std::stoll(argv[++i]);
                if (val <= 0) {
                    std::cerr << "[Error] --ops must be strictly positive (> 0); got: " << val << "\n";
                    return 1;
                }
                workloads.push_back(static_cast<std::size_t>(val));
                custom_workload = true;
            } catch (const std::exception&) {
                std::cerr << "[Error] Invalid value for --ops: " << argv[i] << "\n";
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

    if (!custom_workload) {
        workloads = {10000, 100000, 500000};
    }

    std::cout << "\n============================================================\n";
    std::cout << "    HFT Simulation Engine: Order Execution Benchmark       \n";
    std::cout << "============================================================\n";
    std::cout << "  Repetitions        : " << repetitions << "\n";
    std::cout << "  Throughput Sizes   : ";
    for (std::size_t idx = 0; idx < workloads.size(); ++idx) {
        std::cout << workloads[idx] << (idx + 1 < workloads.size() ? ", " : "");
    }
    std::cout << "\n";
    std::cout << "  Batched Latency    : " << batch_count << " batches of " << batch_size << " ops\n";
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
            std::cout << ">>> Running Repetition " << rep << " of " << repetitions << "\n\n";
        }

        // 1. Resting Orders Throughput
        std::cout << "--- Phase 1: Resting Orders (No Match) Throughput ---\n";
        for (std::size_t size : workloads) {
            std::cout << "Running resting orders benchmark for " << size << " orders...\n";
            try {
                BenchmarkResult res = run_resting_orders_throughput(size);
                if (repetitions > 1) {
                    res.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
                }
                print_benchmark_result(res);
                all_results.push_back(res);
            } catch (const std::exception& ex) {
                std::cerr << "[Error] Failed resting orders benchmark for " << size << ": " << ex.what() << "\n";
                return 1;
            }
        }

        // 2. Matching Orders Throughput
        std::cout << "--- Phase 2: Matching Orders (Trade Generation) Throughput ---\n";
        for (std::size_t size : workloads) {
            std::cout << "Running matching orders benchmark for " << size << " pairs (" << (size * 2) << " orders)...\n";
            try {
                BenchmarkResult res = run_matching_orders_throughput(size);
                if (repetitions > 1) {
                    res.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
                }
                print_benchmark_result(res);
                all_results.push_back(res);
            } catch (const std::exception& ex) {
                std::cerr << "[Error] Failed matching orders benchmark for " << size << ": " << ex.what() << "\n";
                return 1;
            }
        }

        // 3. Batched Resting Order Latency Distribution
        std::cout << "--- Phase 3: Resting Order Batched Effective Latency (" << batch_count << " batches of " << batch_size << " ops) ---\n";
        try {
            std::size_t warmup_batches = (warmup_count / batch_size > 0) ? (warmup_count / batch_size) : 10;
            BenchmarkResult lat_batch_rest = run_resting_order_batched_latency(batch_size, batch_count, warmup_batches);
            if (repetitions > 1) {
                lat_batch_rest.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
            }
            print_benchmark_result(lat_batch_rest);
            all_results.push_back(lat_batch_rest);
        } catch (const std::exception& ex) {
            std::cerr << "[Error] Failed resting order batched latency benchmark: " << ex.what() << "\n";
            return 1;
        }

        // 4. Batched Matching Order Latency Distribution
        std::cout << "--- Phase 4: Matching Order Batched Effective Latency (" << batch_count << " batches of " << batch_size << " trades) ---\n";
        try {
            std::size_t warmup_batches = (warmup_count / batch_size > 0) ? (warmup_count / batch_size) : 10;
            BenchmarkResult lat_batch_match = run_matching_order_batched_latency(batch_size, batch_count, warmup_batches);
            if (repetitions > 1) {
                lat_batch_match.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
            }
            print_benchmark_result(lat_batch_match);
            all_results.push_back(lat_batch_match);
        } catch (const std::exception& ex) {
            std::cerr << "[Error] Failed matching order batched latency benchmark: " << ex.what() << "\n";
            return 1;
        }

        // 5. Direct Resting Order Latency Distribution
        std::cout << "--- Phase 5: Direct Resting Order Latency (" << latency_samples << " samples) ---\n";
        try {
            BenchmarkResult lat_rest = run_resting_order_direct_latency(latency_samples, warmup_count);
            if (repetitions > 1) {
                lat_rest.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
            }
            print_benchmark_result(lat_rest);
            all_results.push_back(lat_rest);
        } catch (const std::exception& ex) {
            std::cerr << "[Error] Failed resting order direct latency benchmark: " << ex.what() << "\n";
            return 1;
        }

        // 6. Direct Matching Order Latency Distribution
        std::cout << "--- Phase 6: Direct Matching Order Latency (" << latency_samples << " samples) ---\n";
        try {
            BenchmarkResult lat_match = run_matching_order_direct_latency(latency_samples, warmup_count);
            if (repetitions > 1) {
                lat_match.notes += " [Rep " + std::to_string(rep) + "/" + std::to_string(repetitions) + "]";
            }
            print_benchmark_result(lat_match);
            all_results.push_back(lat_match);
        } catch (const std::exception& ex) {
            std::cerr << "[Error] Failed matching order direct latency benchmark: " << ex.what() << "\n";
            return 1;
        }
    }

    // 7. Write CSV output
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
