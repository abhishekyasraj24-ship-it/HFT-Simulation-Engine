# Architecture Overview

This document outlines the planned architecture for the HFT-Simulation-Engine. The system is designed to simulate the flow of market data and the execution of trades in a simplified electronic trading environment.

## Data Flow

1. **Market Data Simulation**
   - Generates simulated market data (L2 order book updates, trades).
   - This data is fed into the main trading engine.

2. **Strategy / Signal Generation**
   - Listens to the market data feed.
   - Evaluates simple conditions (signals) to decide whether to buy or sell.

3. **Risk & Portfolio Management**
   - Before an order is sent to the market, it passes through a risk check.
   - Ensures the strategy doesn't exceed predefined position limits or risk thresholds.

4. **Execution / Order Routing**
   - Simulates sending the order to a matching engine (exchange).
   - Tracks the status of the order (e.g., pending, filled, canceled).

5. **Inter-Process Communication (IPC)**
   - The C++ engine handles the latency-sensitive tasks (data processing, strategy, risk, execution).
   - Important events (like trades executed, order book snapshots, PnL updates) are published via an IPC mechanism (initially planned as operating-system pipes).

6. **Visualization (Python)**
   - Subscribes to the data published via IPC.
   - Renders a dashboard or charts to help visualize the simulation in real-time.
