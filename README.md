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

## Planned Features
- **IPC Layer:** Communication mechanism between the C++ engine and the Python visualizer. The initial implementation will use operating-system pipes to stream events, with other mechanisms explored later.
- **Visualization Dashboard (Python):** A real-time or near-real-time view of market conditions, order book state, and simulated PnL.

## Building and Running

### Prerequisites
A modern C++ compiler supporting C++17 or higher (such as `clang++` or `g++`).
You can check if a compiler is available on your machine by running:
```bash
c++ --version
# or
g++ --version
# or
clang++ --version
```

### Build Main Engine
Compile the engine with all modules (market data, strategy, execution engine, portfolio):
```bash
mkdir -p bin
c++ -std=c++17 -Wall -Wextra -O2 engine/MarketData.cpp engine/Strategy.cpp engine/OrderBook.cpp engine/ExecutionEngine.cpp engine/Portfolio.cpp engine/main.cpp -o bin/market_data
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

### Build and Run Strategy Tests
Compile and run the standalone strategy unit test suite:
```bash
c++ -std=c++17 -Wall -Wextra -O2 engine/MarketData.cpp engine/Strategy.cpp tests/test_strategy.cpp -o bin/test_strategy
./bin/test_strategy
```

### Build and Run Order Tests
Compile and run the standalone order representation unit test suite:
```bash
c++ -std=c++17 -Wall -Wextra -O2 tests/test_order.cpp -o bin/test_order
./bin/test_order
```

### Build and Run Execution Engine Tests
Compile and run the matching engine and order book test suite:
```bash
c++ -std=c++17 -Wall -Wextra -O2 engine/OrderBook.cpp engine/ExecutionEngine.cpp tests/test_execution_engine.cpp -o bin/test_execution_engine
./bin/test_execution_engine
```

### Build and Run Portfolio Tests
Compile and run the portfolio and P&L unit test suite:
```bash
c++ -std=c++17 -Wall -Wextra -O2 engine/Portfolio.cpp tests/test_portfolio.cpp -o bin/test_portfolio
./bin/test_portfolio
```
