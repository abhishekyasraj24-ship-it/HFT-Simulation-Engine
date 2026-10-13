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

## Planned Features
- **Visualization Dashboard (Python):** A real-time terminal or graphical dashboard consuming IPC streams to display live charts, order book depth, and P&L progression.

## Building and Running

### Prerequisites
- A modern C++ compiler supporting C++17 or higher (`clang++` or `g++`).
- Python 3.8+ (using standard library only).

```bash
c++ --version
python3 --version
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

### Build and Run IPC Producer (Module 7)
Compile the standalone C++ IPC producer binary:
```bash
mkdir -p bin
c++ -std=c++17 -Wall -Wextra -O2 \
    engine/MarketData.cpp engine/OrderBook.cpp \
    engine/ExecutionEngine.cpp engine/Portfolio.cpp \
    engine/RiskManager.cpp engine/IPCProtocol.cpp \
    engine/ipc_producer.cpp -o bin/ipc_producer
```

### Run End-to-End C++ ↔ Python IPC Demonstration
Launch the full pipeline where Python executes the C++ producer, consumes standard output via an OS pipe, and prints incoming events in real time:
```bash
python3 visualizer/run_ipc_demo.py
```

Options:
```bash
# Customize tick count, seed, or executable path:
python3 visualizer/run_ipc_demo.py --ticks 20 --seed 100 --price 120.0
```

Expected high-level output:
```text
=================================================================
   HFT Simulation Engine: C++ ↔ Python IPC Demonstration
=================================================================
Producer Executable : /path/to/bin/ipc_producer
Producer Arguments  : --ticks 10 --seed 42 --price 100.0 --cash 10000.0
-----------------------------------------------------------------
Listening on standard output OS pipe...
-----------------------------------------------------------------
[STREAM START] Protocol Version: 1
[PORTFOLIO] Pos: 0 units | Cash: $10000.00 | AvgCost: $0.00 | Realized PnL: $+0.00 | Unrealized PnL: $+0.00 | Total PnL: $+0.00
[TICK #01] Market Price: $100.2965
[TICK #02] Market Price: $99.9800
[TICK #03] Market Price: $100.2597
[TRADE #1] 30 units @ $100.2597 (Buyer #2, Seller #1)
[PORTFOLIO] Pos: 30 units | Cash: $6992.21 | AvgCost: $100.26 | Realized PnL: $+0.00 | Unrealized PnL: $+0.00 | Total PnL: $+0.00
...
[STREAM END] End record received (v1)
-----------------------------------------------------------------
=== IPC CONSUMPTION SUMMARY ===
Protocol Version   : 1
Ticks Received     : 10
Trades Received    : 2
Portfolio Updates  : 3
End Record Received: Yes
Producer Exit Code : 0
Total Messages     : 17
=================================================================
```

### Troubleshooting
- **Producer binary not found (`FileNotFoundError`):** Ensure you ran the build step for `bin/ipc_producer` before running `run_ipc_demo.py`. You can explicitly pass the binary path using `--producer-bin <path>`.
- **Pipe Deadlock Prevention:** The Python launcher drains `stderr` on a dedicated background thread while reading `stdout` incrementally on the main thread. This prevents standard OS pipe buffer exhaustion from blocking either process.
- **Malformed Protocol Line (`ProtocolError`):** Ensure custom C++ producers write only tab-separated records to `stdout` and flush after every message. All logs must go to `stderr`.

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

### Run IPC Protocol Python Tests (Module 7)
```bash
python3 -m unittest tests/test_ipc_consumer.py
```
