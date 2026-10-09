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
- Architecture documentation outlined.

## Planned Features
- **Market Data Feed Simulator:** Generates mock order book updates and trades.
- **Trading Engine (C++):** A core engine to process incoming market data and simulate order execution.
- **Risk & Portfolio Management:** Basic checks for simulated positions and exposure.
- **IPC Layer:** Communication mechanism between the C++ engine and the Python visualizer. The initial implementation will use operating-system pipes to stream events, with other mechanisms explored later.
- **Visualization Dashboard (Python):** A real-time or near-real-time view of market conditions, order book state, and simulated PnL.
