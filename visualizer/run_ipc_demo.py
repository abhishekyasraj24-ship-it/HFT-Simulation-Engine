#!/usr/bin/env python3
"""Launcher script for Module 7: C++ <-> Python IPC demonstration.

Spawns the compiled C++ producer binary via an OS pipe, consumes the protocol
stream incrementally, prints incoming events in real time, and displays a
clean completion summary.
"""

import argparse
import os
from pathlib import Path
import sys

# Ensure visualizer directory is in path for imports
current_dir = Path(__file__).resolve().parent
if str(current_dir) not in sys.path:
    sys.path.insert(0, str(current_dir))

from ipc_consumer import (  # noqa: E402
    IPCLauncher,
    IPCProtocolConsumer,
    IPCMessage,
    VersionMessage,
    TickMessage,
    TradeMessage,
    PortfolioMessage,
    EndMessage,
    IPCError,
)


def format_message(msg: IPCMessage) -> str:
    """Formats an IPC message into a human-readable visualizer event line."""
    if isinstance(msg, VersionMessage):
        return f"[STREAM START] Protocol Version: {msg.version}"
    elif isinstance(msg, TickMessage):
        return f"[TICK #{msg.tick_number:02d}] Market Price: ${msg.price:.4f}"
    elif isinstance(msg, TradeMessage):
        return (
            f"[TRADE #{msg.trade_id}] {msg.quantity} units @ ${msg.price:.4f} "
            f"(Buyer #{msg.buy_order_id}, Seller #{msg.sell_order_id})"
        )
    elif isinstance(msg, PortfolioMessage):
        return (
            f"[PORTFOLIO] Pos: {msg.position_quantity} units | "
            f"Cash: ${msg.cash:.2f} | AvgCost: ${msg.average_entry_price:.2f} | "
            f"Realized PnL: ${msg.realized_pnl:+.2f} | "
            f"Unrealized PnL: ${msg.unrealized_pnl:+.2f} | "
            f"Total PnL: ${msg.total_pnl:+.2f}"
        )
    elif isinstance(msg, EndMessage):
        return f"[STREAM END] End record received (v{msg.version})"
    return str(msg)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run C++ to Python IPC stream demonstration using OS pipes."
    )
    parser.add_argument(
        "--producer-bin",
        type=str,
        default=None,
        help="Path to compiled C++ producer executable (default: auto-detected in ./bin/ or ./build/)",
    )
    parser.add_argument(
        "--ticks",
        type=int,
        default=10,
        help="Number of simulated market ticks to generate (default: 10)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed for reproducible simulation (default: 42)",
    )
    parser.add_argument(
        "--price",
        type=float,
        default=100.0,
        help="Starting market price (default: 100.0)",
    )
    parser.add_argument(
        "--cash",
        type=float,
        default=10000.0,
        help="Starting portfolio cash (default: 10000.0)",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress real-time event printing, only show final summary",
    )

    args = parser.parse_args()

    # Build argument list for C++ binary
    producer_args = [
        "--ticks", str(args.ticks),
        "--seed", str(args.seed),
        "--price", str(args.price),
        "--cash", str(args.cash),
    ]

    print("=" * 65)
    print("   HFT Simulation Engine: C++ ↔ Python IPC Demonstration   ")
    print("=" * 65)

    try:
        launcher = IPCLauncher(
            executable_path=args.producer_bin,
            args=producer_args,
        )
        print(f"Producer Executable : {launcher.executable_path}")
        print(f"Producer Arguments  : {' '.join(producer_args)}")
        print("-" * 65)
        print("Listening on standard output OS pipe...")
        print("-" * 65)

        def on_msg(m: IPCMessage) -> None:
            if not args.quiet:
                print(format_message(m))

        result = launcher.run(on_message=on_msg)

        counts = result["counts"]
        print("-" * 65)
        print("=== IPC CONSUMPTION SUMMARY ===")
        print(f"Protocol Version   : {counts.get('VERSION', 0)}")
        print(f"Ticks Received     : {counts.get('TICK', 0)}")
        print(f"Trades Received    : {counts.get('TRADE', 0)}")
        print(f"Portfolio Updates  : {counts.get('PORTFOLIO', 0)}")
        print(f"End Record Received: {'Yes' if counts.get('END', 0) > 0 else 'No'}")
        print(f"Producer Exit Code : {result['exit_code']}")
        print(f"Total Messages     : {sum(counts.values())}")
        if result["stderr"]:
            print(f"Producer Diagnostics (stderr):\n{result['stderr']}")
        print("=" * 65)
        print("IPC demonstration completed successfully.")
        return 0

    except FileNotFoundError as ex:
        print(f"\n[ERROR] Producer executable not found: {ex}", file=sys.stderr)
        print("Please compile the C++ producer first using:", file=sys.stderr)
        print("  c++ -std=c++17 -Wall -Wextra -O2 engine/MarketData.cpp engine/OrderBook.cpp "
              "engine/ExecutionEngine.cpp engine/Portfolio.cpp engine/RiskManager.cpp "
              "engine/IPCProtocol.cpp engine/ipc_producer.cpp -o bin/ipc_producer", file=sys.stderr)
        return 1
    except IPCError as ex:
        print(f"\n[IPC ERROR] {ex}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n[INTERRUPTED] Consumer aborted by user.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
