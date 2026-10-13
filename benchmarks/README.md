# Module 9: Performance Benchmarking and Latency Measurement

This directory contains the reproducible C++ performance benchmark suite for the **HFT-Simulation-Engine**. It measures simulation throughput and operation latency for the core simulation components under isolated, deterministic conditions.

---

## 1. Overview and Architecture

The benchmark suite consists of:

- **`BenchmarkUtils.hpp`**: Shared benchmarking utilities providing:
  - `do_not_optimize<T>()`: Compiler optimization barrier using inline assembly (`asm volatile`) preventing dead-code elimination.
  - `calculate_latency_stats()`: Accurate statistical distribution calculation (min, max, mean, median/p50, p95, p99).
  - RFC 4180 compliant CSV serialization and file writing into `benchmark_results/`.
- **`benchmark_market_data.cpp`**: Standalone executable measuring `MarketDataGenerator` throughput and per-tick generation latency across configurable workloads (default: 100k, 1M, 5M ticks).
- **`benchmark_order_execution.cpp`**: Standalone executable measuring `ExecutionEngine` and `OrderBook` throughput and latency for resting limit orders and matching market orders across configurable workloads (default: 10k, 100k, 500k operations).
- **`../tests/test_benchmark.cpp`**: Unit test suite validating percentiles calculation, edge-case rejection (empty samples, division by zero), CSV escaping, determinism, and engine mechanics.

---

## 2. Compilation Instructions

Compile with C++17 and `-O3` optimization:

```bash
mkdir -p bin benchmark_results

# Market Data Benchmark
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/MarketData.cpp benchmarks/benchmark_market_data.cpp \
    -o bin/benchmark_market_data

# Order Execution Benchmark
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/OrderBook.cpp engine/ExecutionEngine.cpp \
    benchmarks/benchmark_order_execution.cpp \
    -o bin/benchmark_order_execution

# Benchmark Utilities Unit Test
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/MarketData.cpp engine/OrderBook.cpp engine/ExecutionEngine.cpp \
    tests/test_benchmark.cpp \
    -o bin/test_benchmark
```

---

## 3. Running Benchmarks

### 3.1 Market Data Benchmark

```bash
# Run standard suite (100k, 1M, 5M ticks + 100k latency samples)
./bin/benchmark_market_data

# Run with custom tick count and seed
./bin/benchmark_market_data --ticks 1000000 --seed 42

# Customize latency sample count and warm-up iterations
./bin/benchmark_market_data --ticks 500000 --latency-samples 200000 --warmup 20000

# Specify custom CSV output path
./bin/benchmark_market_data --output benchmark_results/custom_market_data.csv

# View all options
./bin/benchmark_market_data --help
```

### 3.2 Order Execution Benchmark

```bash
# Run standard suite (10k, 100k, 500k operations for resting and matching)
./bin/benchmark_order_execution

# Run custom workload size
./bin/benchmark_order_execution --ops 200000

# Customize latency sample count and warm-up
./bin/benchmark_order_execution --ops 100000 --latency-samples 100000 --warmup 10000

# View all options
./bin/benchmark_order_execution --help
```

### 3.3 Running Benchmark Unit Tests

```bash
./bin/test_benchmark
```

---

## 4. Benchmark Methodology & Latency Measurement Audit

### 4.1 Clock Characteristics and the 42 ns Discrepancy
When measuring individual operations whose execution time is extremely short (e.g. `MarketDataGenerator::next_tick()` at ~4.8 ns):
- **Hardware Counter Frequency**: On macOS ARM64 (Apple Silicon), `std::chrono::steady_clock` reads the virtual counter register `CNTVCT_EL0`, which runs at a fixed frequency of 24 MHz ($1 / 24,000,000 \approx 41.667\text{ ns}$).
- **Hardware Quantum**: The counter increments exclusively in discrete quanta of $41.67\text{ ns}$. Any time delta measured by two consecutive reads will always report $0\text{ ns}, 41.67\text{ ns}, 83.33\text{ ns}, \dots$.
- **Call Overhead**: Calling `steady_clock::now()` incurs an instruction and register overhead of ~15–25 ns per call (~31 ns combined for start and end timestamps).
- **The Result**: A ~4.8 ns operation cannot be resolved by an unbatched start/stop timer pair. In ~70% of iterations the counter does not tick ($0\text{ ns}$ recorded); in ~30% it ticks once ($42\text{ ns}$ recorded). This explains 100% of the statistical clustering around 42 ns.

### 4.2 Defensible Measurement Approaches
To provide accurate, reproducible metrics without subtracting speculative synthetic overheads:
1. **Batch Throughput (Macro-measurement)**: Times a large contiguous sequence of operations ($100\text{k} - 5\text{M}$ iterations) with a single start/stop pair. The 31 ns timer read overhead is paid only once over millions of operations ($<0.00003\text{ ns/op}$), completely eliminating clock read overhead and quantization error.
2. **Batched Effective Latency (Micro-distribution)**: Times mini-batches of $B$ operations ($B=1000$ for market data, $B=100$ for order execution) and records $(\Delta t_{\text{batch}} / B)$ as each sample.
   - For $B=1000$ (batch duration ~4.8 µs), the batch spans over 115 hardware clock ticks, reducing the quantization error to $\pm 0.042\text{ ns/op}$ and amortizing timer overhead to $\sim 0.031\text{ ns/op}$ (<1% error).
   - Captures true operational latency distributions (min, median, p95, p99) under cache, scheduler, and workload variation.
3. **Direct Single-Operation Latency (Reference)**: Retained for transparency, but explicitly labeled as *Hardware Discretized* to reflect the host's 41.67 ns timer quantum and ~31 ns call overhead.

### 4.3 Percentile Convention
All percentile metrics are calculated using the standard **NIST / Wikipedia Nearest Rank Method**:
$$\text{rank} = \left\lceil \frac{P}{100} \times N \right\rceil, \quad \text{index} = \min(\text{rank} - 1, N - 1)$$
- For odd $N$, the median (p50) is the exact middle element: $\text{samples}[N / 2]$.
- For even $N$, the median is the midpoint of the two central elements: $(\text{samples}[N/2 - 1] + \text{samples}[N/2]) / 2.0$.

---

## 5. Metric Definitions

- **Operations Processed**: Count of simulated units executed (ticks generated or orders submitted).
- **Trades Generated**: Actual count of trade execution records returned by `ExecutionEngine::submit_order()`.
- **Elapsed Wall Time (s)**: High-resolution wall-clock duration measured via `std::chrono::steady_clock`.
- **Throughput (ops/sec)**: Calculated as `operations_count / elapsed_seconds`.
- **Trade Rate (trades/sec)**: Calculated as `trades_count / elapsed_seconds` for matching workloads.
- **Latency Percentiles (ns / µs)**:
  - **Median (p50)**: Time within which 50% of operations completed.
  - **p95**: Time within which 95% of operations completed.
  - **p99**: Time within which 99% of operations completed.

---

## 6. Local Benchmark Results (Apple M-Series, macOS, `-O3`)

> **Note**: Measured locally across multiple repetitions with fixed seeds. Results reflect local CPU frequency scaling, cache warm-up, and host load.

### Market Data Generator
| Workload | Operations | Elapsed Time | Throughput / Latency |
|---|---|---|---|
| Throughput (100k ticks) | 100,000 | 0.000375 s | 266.37M ops/sec (~3.75 ns/tick) |
| Throughput (1M ticks) | 1,000,000 | 0.003740 s | 267.36M ops/sec (~3.74 ns/tick) |
| Throughput (5M ticks) | 5,000,000 | 0.019182 s | 260.67M ops/sec (~3.84 ns/tick) |
| Batched Latency (1k batches × 1k ticks) | 1,000,000 | 0.003659 s | Min: 3.25 ns, **Median: 3.67 ns**, p95: 4.58 ns, p99: 4.67 ns |
| Direct Latency (100k samples, discretized) | 100,000 | 0.002766 s | Min: 0.00 ns, Median: 0.00 ns, p95: 42.00 ns, p99: 42.00 ns |

### Order Execution Engine
| Workload | Operations | Trades | Elapsed Time | Throughput | Latency (Median / p95 / p99) |
|---|---|---|---|---|---|
| Resting Orders (10k ops) | 10,000 | 0 | 0.000181 s | 55.38M ops/sec | — |
| Resting Orders (100k ops) | 100,000 | 0 | 0.001337 s | 74.81M ops/sec | — |
| Resting Orders (500k ops) | 500,000 | 0 | 0.006685 s | 74.80M ops/sec | — |
| Matching Orders (10k pairs / 20k ops) | 20,000 | 10,000 | 0.000822 s | 24.34M ops/sec | 12.17M trades/sec |
| Matching Orders (100k pairs / 200k ops) | 200,000 | 100,000 | 0.007467 s | 26.79M ops/sec | 13.39M trades/sec |
| Matching Orders (500k pairs / 1M ops) | 1,000,000 | 500,000 | 0.037033 s | 27.00M ops/sec | 13.50M trades/sec |
| Batched Resting Latency (1k × 100 ops) | 100,000 | 0 | 0.001225 s | 81.64M ops/sec | **Median: 10.83 ns**, p95: 12.50 ns, p99: 52.91 ns |
| Batched Matching Latency (1k × 100 trades)| 100,000 | 100,000 | 0.003250 s | 30.77M ops/sec | **Median: 21.66 ns**, p95: 27.50 ns, p99: 30.00 ns |
| Direct Resting Latency (100k samples) | 100,000 | 0 | 0.003707 s | 26.97M ops/sec | Median: 0.00 ns, p95: 42.00 ns, p99: 42.00 ns |
| Direct Matching Latency (100k samples) | 100,000 | 100,000 | 0.008206 s | 12.19M ops/sec | Median: 42.00 ns, p95: 42.00 ns, p99: 83.00 ns |

---

## 7. Caveats and Limitations

1. **Hardware Timer Quantum**: On ARM64 macOS platforms, timestamps can only resolve at 41.67 ns multiples (24 MHz counter). Sub-quantum operations (~4.8 ns) must be evaluated via batch throughput or amortized mini-batch latency distributions.
2. **Educational In-Memory Simulation**: This engine operates purely in cache-resident RAM. It does not measure network transit, kernel-bypass NIC (Solarflare EF_VI / DPDK) latency, PCI-e bus traversal, or exchange matching engine gateway delays.
3. **Execution Jitter**: Microbenchmark distributions exhibit variance across runs due to CPU power state transitions, thread migration, and operating system background interrupts. Fixed seeds and multi-run averages should be utilized when comparing changes.
