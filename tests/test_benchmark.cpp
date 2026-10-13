#include "../benchmarks/BenchmarkUtils.hpp"
#include "../engine/ExecutionEngine.hpp"
#include "../engine/MarketData.hpp"
#include "../engine/Order.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace hft::benchmarks;

#define ASSERT_THROWS(expr, ExpectedException) \
    do { \
        bool caught = false; \
        try { \
            expr; \
        } catch (const ExpectedException&) { \
            caught = true; \
        } catch (...) { \
        } \
        assert(caught && "Expected exception was not thrown: " #expr); \
    } while (0)

void test_latency_calculation_known_distribution() {
    std::cout << "[Test 1] Testing calculate_latency_stats with known 1..100 distribution...\n";

    std::vector<uint64_t> samples;
    samples.reserve(100);
    for (uint64_t i = 1; i <= 100; ++i) {
        samples.push_back(i);
    }

    LatencyStats stats = calculate_latency_stats(samples);
    assert(stats.sample_count == 100);
    assert(stats.min_ns == 1.0);
    assert(stats.max_ns == 100.0);
    assert(std::abs(stats.mean_ns - 50.5) < 1e-6);
    assert(std::abs(stats.median_ns - 50.5) < 1e-6);
    assert(stats.p95_ns == 95.0);
    assert(stats.p99_ns == 99.0);

    std::cout << "  -> PASSED: Mean, median, p95, p99 verified.\n\n";
}

void test_latency_calculation_edge_cases() {
    std::cout << "[Test 2] Testing calculate_latency_stats edge cases (empty vector, single sample)...\n";

    // 1. Empty vector must throw std::invalid_argument
    std::vector<uint64_t> empty_samples;
    ASSERT_THROWS(calculate_latency_stats(empty_samples), std::invalid_argument);

    // 2. Single sample
    std::vector<uint64_t> single_sample = {42};
    LatencyStats stats = calculate_latency_stats(single_sample);
    assert(stats.sample_count == 1);
    assert(stats.min_ns == 42.0);
    assert(stats.max_ns == 42.0);
    assert(stats.mean_ns == 42.0);
    assert(stats.median_ns == 42.0);
    assert(stats.p95_ns == 42.0);
    assert(stats.p99_ns == 42.0);

    // 3. Odd number of samples
    std::vector<uint64_t> odd_samples = {10, 20, 30};
    LatencyStats odd_stats = calculate_latency_stats(odd_samples);
    assert(odd_stats.sample_count == 3);
    assert(odd_stats.median_ns == 20.0);

    std::cout << "  -> PASSED: Empty rejection and boundary cases verified.\n\n";
}

void test_csv_escaping() {
    std::cout << "[Test 3] Testing RFC 4180 CSV escaping...\n";

    // Simple string without special chars
    assert(escape_csv_field("simple_text") == "simple_text");

    // String with comma
    assert(escape_csv_field("hello,world") == "\"hello,world\"");

    // String with quotes
    assert(escape_csv_field("quote \"test\"") == "\"quote \"\"test\"\"\"");

    // String with newline
    assert(escape_csv_field("multi\nline") == "\"multi\nline\"");

    // String with comma and quote
    assert(escape_csv_field("a, \"b\"") == "\"a, \"\"b\"\"\"");

    std::cout << "  -> PASSED: Quotes, commas, and newlines escaped correctly.\n\n";
}

void test_csv_formatting_and_file_io() {
    std::cout << "[Test 4] Testing CSV row formatting and file saving...\n";

    BenchmarkResult res;
    res.benchmark_name = "TestBench";
    res.workload_type = "Sample, Type";
    res.workload_size = 1000;
    res.operations_count = 1000;
    res.trades_count = 50;
    res.elapsed_seconds = 0.005;
    res.throughput_ops_sec = 200000.0;
    res.throughput_trades_sec = 10000.0;
    res.seed = 42;
    res.notes = "Sample \"test\" note";

    std::vector<uint64_t> samples = {10, 20, 30, 40, 50};
    res.latency = calculate_latency_stats(samples);

    std::string row = format_csv_row(res);
    assert(!row.empty());
    assert(row.find("TestBench") != std::string::npos);
    assert(row.find("\"Sample, Type\"") != std::string::npos);
    assert(row.find("\"Sample \"\"test\"\" note\"") != std::string::npos);

    // Test saving to a test directory
    std::string test_csv = "benchmark_results/test_output.csv";
    std::vector<BenchmarkResult> batch = {res};
    save_results_to_csv(test_csv, batch, false);

    assert(std::filesystem::exists(test_csv));
    assert(std::filesystem::file_size(test_csv) > 0);

    // Read back and verify lines
    std::ifstream ifs(test_csv);
    std::string header_line, data_line;
    std::getline(ifs, header_line);
    std::getline(ifs, data_line);

    assert(header_line == get_csv_header());
    assert(data_line == row);

    // Clean up temporary test file
    std::filesystem::remove(test_csv);

    std::cout << "  -> PASSED: Header and data rows formatted and written properly.\n\n";
}

void test_deterministic_market_data_generator() {
    std::cout << "[Test 5] Testing determinism of MarketDataGenerator with fixed seed...\n";

    MarketDataGenerator gen1(100.0, 100, 42);
    MarketDataGenerator gen2(100.0, 100, 42);

    for (std::size_t i = 0; i < 100; ++i) {
        MarketTick t1 = gen1.next_tick();
        MarketTick t2 = gen2.next_tick();
        assert(t1.tick_number == t2.tick_number);
        assert(std::abs(t1.price - t2.price) < 1e-9);
    }

    std::cout << "  -> PASSED: Identical seeds produce bitwise-identical price streams.\n\n";
}

void test_execution_engine_benchmark_mechanics() {
    std::cout << "[Test 6] Testing execution engine benchmark mechanics...\n";

    ExecutionEngine engine;

    // 1. Resting order must generate 0 trades
    Order resting = Order::create_limit(1, OrderSide::Sell, 10, 105.0);
    ExecutionResult res1 = engine.submit_order(resting);
    assert(res1.trades.empty());
    assert(res1.order.status == OrderStatus::New);

    // 2. Matching order must execute trade against resting ask
    Order matching = Order::create_limit(2, OrderSide::Buy, 10, 105.0);
    ExecutionResult res2 = engine.submit_order(matching);
    assert(res2.trades.size() == 1);
    assert(res2.trades[0].quantity == 10);
    assert(res2.trades[0].price == 105.0);
    assert(res2.order.status == OrderStatus::Filled);

    std::cout << "  -> PASSED: Resting and matching logic verified.\n\n";
}

void test_clock_probe() {
    std::cout << "[Test 7] Testing clock probe utility (probe_clock_characteristics)...\n";

    ClockCharacteristics info = probe_clock_characteristics(10000);
    assert(info.is_steady && "Expected std::chrono::steady_clock to be steady");
    assert(info.period_num > 0);
    assert(info.period_den > 0);
    assert(info.call_overhead_ns > 0.0);
    assert(info.resolution_quantum_ns > 0.0);

    std::cout << "  -> Clock probe results: steady=" << info.is_steady
              << ", overhead=" << info.call_overhead_ns << " ns"
              << ", quantum=" << info.resolution_quantum_ns << " ns\n";
    std::cout << "  -> PASSED: System clock probe verified.\n\n";
}

void test_latency_calculation_double_effective() {
    std::cout << "[Test 8] Testing calculate_latency_stats<double> for effective per-op latencies...\n";

    std::vector<double> samples = {4.80, 5.10, 5.20, 5.50, 6.00};
    LatencyStats stats = calculate_latency_stats(samples);
    assert(stats.sample_count == 5);
    assert(std::abs(stats.min_ns - 4.80) < 1e-9);
    assert(std::abs(stats.max_ns - 6.00) < 1e-9);
    assert(std::abs(stats.median_ns - 5.20) < 1e-9);
    assert(std::abs(stats.mean_ns - 5.32) < 1e-9);
    assert(std::abs(stats.p95_ns - 6.00) < 1e-9);
    assert(std::abs(stats.p99_ns - 6.00) < 1e-9);

    std::cout << "  -> PASSED: Floating-point latency distribution verified.\n\n";
}

void test_percentile_nearest_rank_boundary_conventions() {
    std::cout << "[Test 9] Testing Nearest Rank percentile convention boundaries...\n";

    // 1. Array of 10 items (1.0 to 10.0)
    std::vector<double> ten_items;
    for (int i = 1; i <= 10; ++i) {
        ten_items.push_back(static_cast<double>(i));
    }
    LatencyStats stats10 = calculate_latency_stats(ten_items);
    assert(stats10.sample_count == 10);
    // Median for even size: (5 + 6)/2 = 5.5
    assert(std::abs(stats10.median_ns - 5.5) < 1e-9);
    // p95: ceil(0.95 * 10) - 1 = 10 - 1 = 9 -> value 10.0
    assert(std::abs(stats10.p95_ns - 10.0) < 1e-9);
    // p99: ceil(0.99 * 10) - 1 = 10 - 1 = 9 -> value 10.0
    assert(std::abs(stats10.p99_ns - 10.0) < 1e-9);

    // 2. Array of 20 items (1.0 to 20.0)
    std::vector<double> twenty_items;
    for (int i = 1; i <= 20; ++i) {
        twenty_items.push_back(static_cast<double>(i));
    }
    LatencyStats stats20 = calculate_latency_stats(twenty_items);
    assert(stats20.sample_count == 20);
    assert(std::abs(stats20.median_ns - 10.5) < 1e-9);
    // p95: ceil(0.95 * 20) - 1 = 19 - 1 = 18 -> value 19.0
    assert(std::abs(stats20.p95_ns - 19.0) < 1e-9);
    // p99: ceil(0.99 * 20) - 1 = 20 - 1 = 19 -> value 20.0
    assert(std::abs(stats20.p99_ns - 20.0) < 1e-9);

    std::cout << "  -> PASSED: Nearest Rank indexing boundary conditions verified.\n\n";
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "      Running Benchmark Utility Unit Tests          \n";
    std::cout << "====================================================\n\n";

    test_latency_calculation_known_distribution();
    test_latency_calculation_edge_cases();
    test_csv_escaping();
    test_csv_formatting_and_file_io();
    test_deterministic_market_data_generator();
    test_execution_engine_benchmark_mechanics();
    test_clock_probe();
    test_latency_calculation_double_effective();
    test_percentile_nearest_rank_boundary_conventions();

    std::cout << "====================================================\n";
    std::cout << "      ALL BENCHMARK TESTS PASSED SUCCESSFULLY!      \n";
    std::cout << "====================================================\n";
    return 0;
}
