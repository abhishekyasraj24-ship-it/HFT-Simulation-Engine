#!/usr/bin/env python3
"""Module 8: Live Python Trading Visualizer Dashboard using Matplotlib.

Receives real simulation events from the C++ engine producer via OS pipes,
displays live updating charts for market price with buy/sell trade markers,
tracks portfolio P&L over time, displays authoritative portfolio metrics,
and shows engine lifecycle status and trade execution history.
"""

import argparse
import os
from pathlib import Path
import queue
import sys
import threading
from typing import Any, Dict, List, Optional

# Ensure project directories are importable
current_dir = Path(__file__).resolve().parent
if str(current_dir) not in sys.path:
    sys.path.insert(0, str(current_dir))

from dashboard_state import DashboardState, EngineStatus, InferredSide
from ipc_consumer import (
    IPCLauncher,
    IPCMessage,
    ProducerError,
    ProtocolError,
    UnexpectedEOFError,
)

# Matplotlib import with clear troubleshooting message
try:
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation
    import matplotlib.gridspec as gridspec
except ImportError as err:
    print(
        "\n[ERROR] Matplotlib is required for the live trading visualizer dashboard.\n"
        "Please install it using:\n"
        "    pip install matplotlib\n"
        "or\n"
        "    python3 -m pip install matplotlib\n",
        file=sys.stderr,
    )
    raise err


class TradingDashboard:
    """Live trading visualization dashboard driven by C++ simulation events."""

    def __init__(
        self,
        launcher: Optional[IPCLauncher] = None,
        state: Optional[DashboardState] = None,
        refresh_interval_ms: int = 100,
        interactive: bool = True,
    ):
        self.launcher = launcher or IPCLauncher()
        self.state = state or DashboardState()
        self.refresh_interval_ms = refresh_interval_ms
        self.interactive = interactive

        # Event queue connecting the IPC reader thread to the GUI main thread
        self.event_queue: queue.Queue[Any] = queue.Queue(maxsize=5000)
        self.worker_thread: Optional[threading.Thread] = None
        self.anim: Optional[FuncAnimation] = None
        self.is_paused: bool = False
        self.finished: bool = False

        # Setup figure and layout
        self._setup_layout()

    def _setup_layout(self) -> None:
        """Initializes the multi-panel Matplotlib figure layout."""
        self.fig = plt.figure(figsize=(15, 8.5), dpi=100)
        self.fig.suptitle(
            "HFT Simulation Engine: Live Trading Visualizer (Module 8)",
            fontsize=15,
            fontweight="bold",
            y=0.98,
        )

        gs = gridspec.GridSpec(2, 2, width_ratios=[1.55, 1.0], height_ratios=[1.2, 1.0], hspace=0.32, wspace=0.22)

        # 1. Price Chart Panel (Top-Left)
        self.ax_price = self.fig.add_subplot(gs[0, 0])
        self.ax_price.set_title("Market Price & Execution Markers", fontsize=11, fontweight="semibold")
        self.ax_price.set_xlabel("Tick Number", fontsize=9)
        self.ax_price.set_ylabel("Price ($)", fontsize=9)
        self.ax_price.grid(True, linestyle="--", alpha=0.5)

        (self.line_price,) = self.ax_price.plot([], [], color="#1f77b4", linewidth=1.5, label="Market Price")
        self.scatter_buy = self.ax_price.scatter([], [], color="#2ca02c", marker="^", s=110, zorder=5, label="Portfolio Buy")
        self.scatter_sell = self.ax_price.scatter([], [], color="#d62728", marker="v", s=110, zorder=5, label="Portfolio Sell")
        self.scatter_unclass = self.ax_price.scatter(
            [], [], facecolors="none", edgecolors="#7f7f7f", marker="o", s=85, linewidths=1.5, zorder=5, label="Trade (Unclassified)"
        )
        self.ax_price.legend(loc="upper left", fontsize=8, framealpha=0.85)

        # 2. Portfolio P&L Chart Panel (Bottom-Left)
        self.ax_pnl = self.fig.add_subplot(gs[1, 0], sharex=self.ax_price)
        self.ax_pnl.set_title("Simulated Portfolio P&L Over Time", fontsize=11, fontweight="semibold")
        self.ax_pnl.set_xlabel("Tick Number", fontsize=9)
        self.ax_pnl.set_ylabel("P&L ($)", fontsize=9)
        self.ax_pnl.grid(True, linestyle="--", alpha=0.5)
        self.ax_pnl.axhline(0.0, color="#888888", linestyle=":", linewidth=1.0)

        (self.line_total_pnl,) = self.ax_pnl.plot([], [], color="#0055d4", linewidth=2.0, label="Total P&L")
        (self.line_realized_pnl,) = self.ax_pnl.plot([], [], color="#2ca02c", linestyle="--", linewidth=1.5, label="Realized P&L")
        (self.line_unrealized_pnl,) = self.ax_pnl.plot([], [], color="#ff7f0e", linestyle=":", linewidth=1.5, label="Unrealized P&L")
        self.ax_pnl.legend(loc="upper left", fontsize=8, framealpha=0.85)

        # 3. Portfolio Metrics Panel (Top-Right)
        self.ax_metrics = self.fig.add_subplot(gs[0, 1])
        self.ax_metrics.axis("off")
        self.text_metrics = self.ax_metrics.text(
            0.04,
            0.92,
            "Awaiting portfolio initialization...",
            fontsize=10,
            family="monospace",
            verticalalignment="top",
            bbox=dict(boxstyle="round,pad=0.6", facecolor="#f8f9fa", edgecolor="#ced4da", linewidth=1.2),
        )

        # 4. Status & Recent Trades Panel (Bottom-Right)
        self.ax_status = self.fig.add_subplot(gs[1, 1])
        self.ax_status.axis("off")
        self.text_status = self.ax_status.text(
            0.04,
            0.92,
            "Engine Status: WAITING",
            fontsize=9.5,
            family="monospace",
            verticalalignment="top",
            bbox=dict(boxstyle="round,pad=0.6", facecolor="#f8f9fa", edgecolor="#ced4da", linewidth=1.2),
        )

        # Connect window events
        self.fig.canvas.mpl_connect("close_event", self._on_close)
        self.fig.canvas.mpl_connect("key_press_event", self._on_key_press)

    def _on_key_press(self, event: Any) -> None:
        """Handles keyboard controls: 'p' or space to pause/resume display refreshes."""
        if event.key in ("p", "P", " "):
            self.is_paused = not self.is_paused
            pause_label = " [PAUSED]" if self.is_paused else ""
            self.fig.suptitle(
                f"HFT Simulation Engine: Live Trading Visualizer (Module 8){pause_label}",
                fontsize=15,
                fontweight="bold",
                y=0.98,
            )
            self.fig.canvas.draw_idle()

    def _on_close(self, _event: Any) -> None:
        """Terminates C++ subprocess when window is closed."""
        self.finished = True
        self.launcher.terminate()

    def start_background_stream(self) -> None:
        """Starts the IPC stream reader on a dedicated background thread."""
        def reader_worker() -> None:
            try:
                def on_msg(m: IPCMessage) -> None:
                    self.event_queue.put(("MESSAGE", m))

                result = self.launcher.run(on_message=on_msg)
                self.event_queue.put(("END", result))
            except Exception as ex:
                self.event_queue.put(("ERROR", ex))

        self.worker_thread = threading.Thread(target=reader_worker, daemon=True)
        self.worker_thread.start()

    def drain_events(self) -> None:
        """Drains all currently queued events into the DashboardState."""
        while not self.event_queue.empty():
            try:
                ev_type, ev_data = self.event_queue.get_nowait()
                if ev_type == "MESSAGE":
                    self.state.process_message(ev_data)
                elif ev_type == "END":
                    # Mark producer completed
                    self.state.producer_completed_successfully = True
                    self.state.status = EngineStatus.COMPLETED
                elif ev_type == "ERROR":
                    self.state.set_error(str(ev_data))
            except queue.Empty:
                break

    def update_frame(self, _frame: int) -> None:
        """Animation update callback invoked on the Matplotlib main thread."""
        # 1. Drain available events from worker thread
        self.drain_events()

        if self.is_paused:
            return

        # 2. Update Price Chart
        if self.state.tick_numbers:
            self.line_price.set_data(self.state.tick_numbers, self.state.prices)

            # Trade markers
            buy_trades = self.state.get_buy_trades()
            if buy_trades:
                self.scatter_buy.set_offsets([[t.tick_number, t.price] for t in buy_trades])
            else:
                self.scatter_buy.set_offsets(empty_offsets())

            sell_trades = self.state.get_sell_trades()
            if sell_trades:
                self.scatter_sell.set_offsets([[t.tick_number, t.price] for t in sell_trades])
            else:
                self.scatter_sell.set_offsets(empty_offsets())

            unclass_trades = self.state.get_unclassified_trades()
            if unclass_trades:
                self.scatter_unclass.set_offsets([[t.tick_number, t.price] for t in unclass_trades])
            else:
                self.scatter_unclass.set_offsets(empty_offsets())

            self.ax_price.relim()
            self.ax_price.autoscale_view()

        # 3. Update P&L Chart
        if self.state.pnl_ticks:
            self.line_total_pnl.set_data(self.state.pnl_ticks, self.state.total_pnl_history)
            self.line_realized_pnl.set_data(self.state.pnl_ticks, self.state.realized_pnl_history)
            self.line_unrealized_pnl.set_data(self.state.pnl_ticks, self.state.unrealized_pnl_history)
            self.ax_pnl.relim()
            self.ax_pnl.autoscale_view()

        # 4. Update Portfolio Metrics Text Box
        port = self.state.latest_portfolio
        if port is not None:
            equity = port.cash + (port.position_quantity * self.state.prices[-1] if self.state.prices else 0.0)
            metrics_str = (
                "PORTFOLIO METRICS (Simulated)\n"
                "────────────────────────────────────────\n"
                f"  Available Cash   : ${port.cash:>12,.2f}\n"
                f"  Position Units   :  {port.position_quantity:>12,d} units\n"
                f"  Avg Entry Price  : ${port.average_entry_price:>12.2f}\n"
                f"  Realized P&L     : ${port.realized_pnl:>+12.2f}\n"
                f"  Unrealized P&L   : ${port.unrealized_pnl:>+12.2f}\n"
                f"  Total P&L        : ${port.total_pnl:>+12.2f}\n"
                "────────────────────────────────────────\n"
                f"  Total Equity     : ${equity:>12,.2f}\n"
            )
        else:
            metrics_str = "PORTFOLIO METRICS\n────────────────────────\nAwaiting initial PORTFOLIO update..."
        self.text_metrics.set_text(metrics_str)

        # 5. Update Status & Trade History Text Box
        status_color = "#007acc"
        if self.state.status == EngineStatus.COMPLETED:
            status_color = "#28a745"
        elif self.state.status == EngineStatus.ERROR:
            status_color = "#dc3545"

        status_header = (
            f"ENGINE LIFECYCLE STATUS\n"
            f"────────────────────────────────────────\n"
            f"  Status           : {self.state.status.value}\n"
            f"  Protocol Version : {self.state.protocol_version or 'N/A'}\n"
            f"  Ticks Consumed   : {self.state.ticks_received}\n"
            f"  Trades Executed  : {self.state.trades_received}\n"
            f"  Completed Cleanly: {'YES' if self.state.producer_completed_successfully else 'NO'}\n"
        )

        if self.state.error_message:
            status_header += f"\n  ERROR: {self.state.error_message[:42]}...\n"

        trades_header = "\nRECENT EXECUTIONS (Last 5)\n────────────────────────────────────────\n"
        if self.state.trades:
            trade_lines = []
            for t in self.state.trades[-5:]:
                trade_lines.append(
                    f"  #{t.trade_id:<2d} | {t.side.value:<4s} {t.quantity:>3d} @ ${t.price:>7.2f} (Tick {t.tick_number})"
                )
            trades_body = "\n".join(trade_lines)
        else:
            trades_body = "  No trades executed yet."

        self.text_status.set_text(status_header + trades_header + trades_body)

        # If completed or errored, stop animation to prevent CPU polling
        if self.state.status in (EngineStatus.COMPLETED, EngineStatus.ERROR):
            if self.anim and self.anim.event_source:
                self.anim.event_source.stop()

    def run(self) -> None:
        """Starts live visualization."""
        self.start_background_stream()

        if self.interactive:
            self.anim = FuncAnimation(
                self.fig,
                self.update_frame,
                interval=self.refresh_interval_ms,
                blit=False,
                cache_frame_data=False,
            )
            try:
                plt.show()
            except KeyboardInterrupt:
                pass
            finally:
                self.launcher.terminate()


def empty_offsets() -> List[List[float]]:
    """Helper returning empty coordinates for scatter plots."""
    return [[float("nan"), float("nan")]]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Launch live Matplotlib trading simulation dashboard (Module 8)."
    )
    parser.add_argument(
        "--producer-bin",
        type=str,
        default=None,
        help="Path to compiled C++ producer binary (default: auto-detected)",
    )
    parser.add_argument(
        "--ticks",
        type=int,
        default=20,
        help="Number of market ticks (default: 20)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Reproducible random seed (default: 42)",
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
        help="Starting portfolio cash balance (default: 10000.0)",
    )
    parser.add_argument(
        "--interval",
        type=int,
        default=100,
        help="Dashboard refresh interval in milliseconds (default: 100)",
    )
    parser.add_argument(
        "--headless",
        action="store_true",
        help="Run without displaying a graphical window and save output plot to disk",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="dashboard_output.png",
        help="Filename to save figure in headless mode (default: dashboard_output.png)",
    )

    args = parser.parse_args()

    producer_args = [
        "--ticks", str(args.ticks),
        "--seed", str(args.seed),
        "--price", str(args.price),
        "--cash", str(args.cash),
    ]

    try:
        launcher = IPCLauncher(executable_path=args.producer_bin, args=producer_args)
    except FileNotFoundError as ex:
        print(f"[ERROR] {ex}", file=sys.stderr)
        return 1

    if args.headless:
        # Use headless non-interactive backend
        plt.switch_backend("Agg")
        dashboard = TradingDashboard(launcher=launcher, refresh_interval_ms=args.interval, interactive=False)
        dashboard.start_background_stream()

        # Wait for worker thread to complete
        if dashboard.worker_thread:
            dashboard.worker_thread.join()

        # Process all remaining events and render final snapshot
        dashboard.update_frame(0)
        dashboard.fig.savefig(args.output, dpi=120)
        print(f"[INFO] Headless dashboard rendered and saved to '{args.output}'.")
        return 0
    else:
        dashboard = TradingDashboard(launcher=launcher, refresh_interval_ms=args.interval, interactive=True)
        dashboard.run()
        return 0


if __name__ == "__main__":
    sys.exit(main())
