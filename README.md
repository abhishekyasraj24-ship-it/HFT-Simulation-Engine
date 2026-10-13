# HFT-Simulation-Engine

A low-latency electronic trading simulation engine designed for learning and exploring the concepts behind high-frequency trading (HFT). 

**Note: This is an educational simulation, not a production trading system.** It explores system design, concurrency, IPC, and latency optimization but does not contain real-world trading logic or execute live market orders.

## Project Goals
- Understand the architecture of a low-latency electronic trading system.
- Explore C++ techniques for performance and low latency.
- Implement basic Inter-Process Communication (IPC) for moving data between components.
- Visualize simulated market data and trading actions using Python.

## Current Implementation Status
- Initial project structure created.
- Architecture documentation outlined (`docs/ARCHITECTURE.md`).
- **Module 1 (Market Data Generator):** Basic C++ random-walk price generator implemented in `engine/` (`MarketData.hpp`, `MarketData.cpp`).
- **Module 2 (Moving Average Crossover Strategy):** Simple Moving Average (SMA) crossover strategy implemented in `engine/` (`Strategy.hpp`, `Strategy.cpp`) and unit tested in `tests/test_strategy.cpp`.
- **Module 3 (Order Representation):** Structured representation of trading orders (`OrderId`, `OrderSide`, `OrderType`, `OrderStatus`, and validation rules) implemented in `engine/Order.hpp` and unit tested in `tests/test_order.cpp`.
- **Module 4 (Order Book & Execution Engine):** Price-Time Priority limit order book (`engine/OrderBook.hpp`, `engine/OrderBook.cpp`), trade records (`engine/Trade.hpp`), and matching engine (`engine/ExecutionEngine.hpp`, `engine/ExecutionEngine.cpp`) supporting Market/Limit orders, partial fills, liquidity cancellation, and limit price protection; unit tested in `tests/test_execution_engine.cpp`.
- **Module 5 (Portfolio & P&L Management):** Long-only portfolio tracker (`engine/Portfolio.hpp`, `engine/Portfolio.cpp`) with VWAP average-cost accounting, realized/unrealized/total P&L, total equity, trade deduplication, and invalid-trade rejection; unit tested in `tests/test_portfolio.cpp`.
- **Module 6 (Risk Management System):** Pre-trade risk gate (`engine/RiskManager.hpp`, `engine/RiskManager.cpp`) that validates proposed orders against configurable `RiskLimits` before execution. Checks include structural order validity, per-order quantity, per-order notional, position-size ceiling, long-only (no short-selling) enforcement, and gross-exposure ceiling. The `RiskManager` is stateless and read-only — it never modifies the portfolio or executes orders. Unit tested in `tests/test_risk_manager.cpp`.
- **Module 7 (C++ ↔ Python IPC Using OS Pipes):** Reliable inter-process communication using operating-system pipes between the C++ engine producer and a Python consumer. Implements a lightweight, versioned, newline-delimited, tab-separated protocol (`engine/IPCProtocol.hpp`, `engine/IPCProtocol.cpp`, `engine/ipc_producer.cpp`) and a pure standard-library Python consumer and launcher (`visualizer/ipc_consumer.py`, `visualizer/run_ipc_demo.py`). Protocol data travels over `stdout` while diagnostic and error logs are isolated to `stderr`. Unit tested in C++ (`tests/test_ipc_protocol.cpp`) and Python (`tests/test_ipc_consumer.py`).
- **Module 8 (Live Python Trading Visualizer):** Real-time Matplotlib trading visualizer (`visualizer/dashboard.py`, `visualizer/dashboard_state.py`) receiving live simulation events from the C++ producer over standard OS pipes. Renders live market prices with classified buy/sell trade markers, tracks portfolio P&L over time, displays authoritative portfolio metrics, and logs execution lifecycle status. Tested in headless and integration modes in `tests/test_dashboard.py`.
- **Module 9 (Performance Benchmarking & Latency Measurement):** High-throughput C++17 benchmarking suite (`benchmarks/benchmark_market_data.cpp`, `benchmarks/benchmark_order_execution.cpp`, `benchmarks/BenchmarkUtils.hpp`) measuring raw simulation throughput (ops/sec) and per-operation latency distributions (median/p50, p95, p99) under isolated, deterministic conditions without I/O or IPC overhead. Saves reproducible machine-readable RFC 4180 CSV reports in `benchmark_results/`. Unit tested in `tests/test_benchmark.cpp`. Full documentation in `benchmarks/README.md`.

### Risk Management Policies (Module 6)

| Limit | Convention | `0` / `0.0` means |
|---|---|---|
| `max_order_quantity` | Max units in a single order (inclusive) | Unlimited |
| `max_order_notional` | Max cash value per order: `qty × price` (inclusive) | Unlimited |
| `max_position_quantity` | Max long position units (inclusive) | Unlimited |
| `max_gross_exposure` | Max `projected_position × ref_market_price` (inclusive) | Unlimited |

**Reference-price policy:**
- *Notional*: limit orders use `limit_price`; market orders use caller-supplied `ref_market_price`.
- *Exposure*: always uses `ref_market_price` × projected position after the proposed order.

**Assumptions:** long-only portfolio, no fees/slippage, single simulated asset, no concurrency.

### IPC Protocol Specification (Module 7)

The C++ engine acts as an event producer, streaming simulation events across standard OS pipes to a Python consumer. Standard error (`stderr`) is strictly reserved for diagnostic logs so it never corrupts the protocol stream on standard output (`stdout`).

#### Message Format
- Records are **newline-delimited** (`\n`), with fields **tab-separated** (`\t`).
- No external serialization or JSON libraries are required.
- Every message defines a message type and an explicit protocol version (`1`).
- Floating-point values are formatted with fixed 6-decimal precision and must be finite (`NaN` and `Inf` are rejected).

#### Supported Message Types and Field Order

| Message Type | Field Count | Field Order & Types | Example |
|---|---|---|---|
| `VERSION` | 2 | `TYPE` (str), `version` (uint) | `VERSION\t1` |
| `TICK` | 4 | `TYPE` (str), `version` (uint), `tick_number` (uint64), `market_price` (float) | `TICK\t1\t1\t100.296543` |
| `TRADE` | 7 | `TYPE` (str), `version` (uint), `trade_id` (uint64), `buy_order_id` (uint64), `sell_order_id` (uint64), `price` (float), `quantity` (uint32) | `TRADE\t1\t1\t2\t1\t100.259669\t30` |
| `PORTFOLIO` | 8 | `TYPE` (str), `version` (uint), `cash` (float), `position_quantity` (uint32), `average_entry_price` (float), `realized_pnl` (float), `unrealized_pnl` (float), `total_pnl` (float) | `PORTFOLIO\t1\t6992.209937\t30\t100.259669\t0.000000\t0.000000\t0.000000` |
| `END` | 2 | `TYPE` (str), `version` (uint) | `END\t1` |

#### Validation Rules & Stream Lifecycle
1. **Initial Announcement:** The stream **must** start with a valid `VERSION` record. Any other message received first raises a protocol violation.
2. **No Duplicates:** Subsequent `VERSION` announcements during an active stream are rejected.
3. **Exact Field Counts:** Each record must contain exactly its specified number of fields. Missing or extra fields trigger rejection.
4. **Finite Floats & Domain Constraints:**
   - Float values must be finite numbers (`math.isfinite` / `std::isfinite`). `nan`, `inf`, `-inf` are rejected.
   - Prices and tick numbers must be strictly positive (`> 0`).
   - Quantities must be positive integers (`> 0` for trades, `>= 0` for positions).
   - `average_entry_price` must be non-negative (`>= 0.0`).
5. **Clean Termination:** The producer concludes with an `END` message. Any message received after `END` is rejected.
6. **Unexpected EOF:** If the stream terminates before receiving `END`, the consumer raises an `UnexpectedEOFError`.
7. **Blank Lines Policy:** Empty or whitespace-only lines are ignored and skipped by default. In strict mode (`ignore_blank_lines=False`), encountering a blank line raises a `ProtocolError`.

### Live Visualizer Dashboard (Module 8)

The Python visualizer dashboard (`visualizer/dashboard.py`) consumes the real-time simulation stream and renders an interactive multi-panel GUI:
- **Price Chart:** Incremental market price against tick number with trade markers.
- **Trade Markers:** Portfolio Buys (green `^`), Portfolio Sells (red `v`), and Unclassified trades (gray `o`) plotted at the execution price and tick.
- **Portfolio P&L Chart:** Real-time plots for Total P&L, Realized P&L, and Unrealized P&L over time.
- **Portfolio Metrics Panel:** Displays authoritative values for Cash, Position, Average Entry Price, Realized P&L, Unrealized P&L, Total P&L, and Total Equity.
- **Status & Execution Log:** Live engine lifecycle status (`RUNNING`, `COMPLETED`, `ERROR`), tick/trade counters, and the 5 most recent executions.

#### Trade-Side Inference Policy & Limitations
The IPC `TRADE` schema reports executed trade IDs, order IDs, price, and quantity, but does not explicitly tag which side belonged to the simulated portfolio.
The dashboard correlates each trade with subsequent `PORTFOLIO` position updates:
- If `position_quantity` increased, the trade is classified as **BUY**.
- If `position_quantity` decreased, the trade is classified as **SELL**.
- If position did not change (or cannot be unambiguously attributed), the trade is classified as **UNCLASSIFIED**.
No artificial trade sides are ever invented.

## Planned Features
- **Interactive Order Injection:** Ability to inject manual buy/sell orders via the visualizer interface into the running matching engine.
- **Order Book Depth Visualization:** Level-2 book depth ladders (bids vs. asks).

## Building and Running

### Prerequisites
- A modern C++ compiler supporting C++17 or higher (`clang++` or `g++`).
- Python 3.8+ (using standard library).
- Matplotlib 3.5+ (for graphical dashboard).

Install Matplotlib:
```bash
pip install matplotlib
# or
python3 -m pip install matplotlib
```

Verify prerequisites:
```bash
c++ --version
python3 --version
python3 -c "import matplotlib; print('Matplotlib version:', matplotlib.__version__)"
```

### Build Main Engine
Compile the engine with all modules (market data, strategy, execution engine, portfolio, risk manager):
```bash
mkdir -p bin
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/MarketData.cpp engine/Strategy.cpp \
    engine/OrderBook.cpp engine/ExecutionEngine.cpp \
    engine/Portfolio.cpp engine/RiskManager.cpp \
    engine/main.cpp -o bin/market_data
```

### Run Main Engine
Run with default values (initial price: $100.00, 20 ticks, non-deterministic random seed):
```bash
./bin/market_data
```

Run with custom parameters (`[initial_price] [total_ticks] [seed]`):
```bash
# Example: Start at $150.00, generate 10 ticks, with reproducible seed 42
./bin/market_data 150.0 10 42
```

### Build IPC Producer (Module 7 & 8)
Compile the standalone C++ IPC producer binary:
```bash
mkdir -p bin
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/MarketData.cpp engine/OrderBook.cpp \
    engine/ExecutionEngine.cpp engine/Portfolio.cpp \
    engine/RiskManager.cpp engine/IPCProtocol.cpp \
    engine/ipc_producer.cpp -o bin/ipc_producer
```

### Run Terminal IPC Demonstration
Launch the terminal stream consumer:
```bash
python3 visualizer/run_ipc_demo.py --ticks 10 --seed 42
```

### Launch Live Trading Visualizer Dashboard (Module 8)
Launch the graphical live dashboard:
```bash
python3 visualizer/dashboard.py
```

Options:
```bash
# Custom parameters: 50 ticks, seed 123, initial price $105.0, refresh interval 100ms
python3 visualizer/dashboard.py --ticks 50 --seed 123 --price 105.0 --interval 100

# Headless mode (renders and saves screenshot to disk without GUI window)
python3 visualizer/dashboard.py --headless --output build/dashboard.png
```

**Interactive Controls:**
- Press `p` or `Space` in the dashboard window to pause or resume display updates. (The background IPC stream continues buffering to ensure the C++ engine is never blocked).
- Close the window or press `Ctrl+C` in the terminal to cleanly terminate the C++ producer subprocess.

### Troubleshooting
- **Missing Matplotlib (`ModuleNotFoundError: No module named 'matplotlib'`):** Install Matplotlib via `pip install matplotlib`.
- **Display/Backend Errors (headless environments):** If running on a headless server or CI without an active X11/Wayland/Cocoa display, pass `--headless` to render and save directly to an image file.
- **Producer binary not found (`FileNotFoundError`):** Compile `bin/ipc_producer` first, or specify `--producer-bin <path>`.
- **Pipe Deadlock Prevention:** `IPCLauncher` drains `stderr` on a dedicated daemon thread while the dashboard drains `stdout` into a thread-safe event queue, preventing OS pipe buffer deadlocks.

---

## Performance Benchmarking (Module 9)

A standalone C++ benchmarking suite to evaluate raw simulation throughput (operations/second) and local single-operation latency distributions (median/p50, p95, p99) under isolated, deterministic conditions. All setup and I/O are kept outside timed loops, and results are written to machine-readable RFC 4180 CSV files in `benchmark_results/`.

For complete documentation on latency isolation, warm-up phases, and methodology, see [`benchmarks/README.md`](benchmarks/README.md).

### Build Benchmark Binaries
```bash
mkdir -p bin benchmark_results

# Market Data Throughput & Latency Benchmark
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/MarketData.cpp benchmarks/benchmark_market_data.cpp \
    -o bin/benchmark_market_data

# Order Execution Throughput & Latency Benchmark
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/OrderBook.cpp engine/ExecutionEngine.cpp \
    benchmarks/benchmark_order_execution.cpp \
    -o bin/benchmark_order_execution
```

### Run Benchmarks
```bash
# Run standard Market Data suite (100k, 1M, 5M ticks + 100k latency samples)
./bin/benchmark_market_data

# Custom workload size or seed
./bin/benchmark_market_data --ticks 1000000 --seed 42 --latency-samples 100000

# Run standard Order Execution suite (10k, 100k, 500k ops for resting and matching)
./bin/benchmark_order_execution

# Custom operations count
./bin/benchmark_order_execution --ops 200000 --latency-samples 100000
```

---

## Running Unit Tests

### Build and Run Strategy Tests (Module 2)
```bash
c++ -std=c++17 -Wall -Wextra -O2 engine/MarketData.cpp engine/Strategy.cpp tests/test_strategy.cpp -o bin/test_strategy
./bin/test_strategy
```

### Build and Run Order Tests (Module 3)
```bash
c++ -std=c++17 -Wall -Wextra -O2 tests/test_order.cpp -o bin/test_order
./bin/test_order
```

### Build and Run Execution Engine Tests (Module 4)
```bash
c++ -std=c++17 -Wall -Wextra -O2 engine/OrderBook.cpp engine/ExecutionEngine.cpp tests/test_execution_engine.cpp -o bin/test_execution_engine
./bin/test_execution_engine
```

### Build and Run Portfolio Tests (Module 5)
```bash
c++ -std=c++17 -Wall -Wextra -O2 engine/Portfolio.cpp tests/test_portfolio.cpp -o bin/test_portfolio
./bin/test_portfolio
```

### Build and Run Risk Manager Tests (Module 6)
```bash
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/Portfolio.cpp engine/RiskManager.cpp \
    tests/test_risk_manager.cpp -o bin/test_risk_manager
./bin/test_risk_manager
```

### Build and Run IPC Protocol C++ Tests (Module 7)
```bash
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/Portfolio.cpp engine/IPCProtocol.cpp \
    tests/test_ipc_protocol.cpp -o bin/test_ipc_protocol
./bin/test_ipc_protocol
```

### Build and Run Benchmark Utilities Unit Tests (Module 9)
```bash
c++ -std=c++17 -Wall -Wextra -Werror -O3 \
    engine/MarketData.cpp engine/OrderBook.cpp engine/ExecutionEngine.cpp \
    tests/test_benchmark.cpp -o bin/test_benchmark
./bin/test_benchmark
```

### Run IPC Protocol Python Tests (Module 7)
```bash
python3 -m unittest tests/test_ipc_consumer.py
```

### Run Visualizer Dashboard Python Tests (Module 8)
```bash
python3 -m unittest tests/test_dashboard.py
```
