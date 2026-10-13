"""Unit and regression tests for Module 8 Live Trading Visualizer.

Covers:
  1. TICK messages update price history correctly.
  2. TRADE messages are recorded correctly.
  3. Buy/sell classification follows documented inference policy.
  4. Unclassifiable trades are not assigned invented sides.
  5. PORTFOLIO messages update every displayed metric.
  6. P&L history updates correctly.
  7. Streams with no trades are handled.
  8. Empty or very short streams are handled.
  9. END produces a completed state.
 10. Malformed input and producer failures produce an error state.
 11. Dashboard runs with non-interactive Matplotlib backend for state/render smoke tests.
 12. End-to-end execution using real C++ producer executable.
"""

from pathlib import Path
import sys
import unittest

# Ensure visualizer directory is in path
project_root = Path(__file__).resolve().parent.parent
visualizer_dir = project_root / "visualizer"
if str(visualizer_dir) not in sys.path:
    sys.path.insert(0, str(visualizer_dir))

# Set Matplotlib to non-interactive headless backend before importing GUI components
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from dashboard import TradingDashboard  # noqa: E402
from dashboard_state import DashboardState, EngineStatus, InferredSide  # noqa: E402
from ipc_consumer import (  # noqa: E402
    EndMessage,
    IPCLauncher,
    PortfolioMessage,
    TickMessage,
    TradeMessage,
    VersionMessage,
)


class TestDashboardState(unittest.TestCase):
    """Deterministic tests for state update and trade inference logic without GUI."""

    def setUp(self) -> None:
        self.state = DashboardState()

    def test_01_tick_updates_price_history(self) -> None:
        """TICK messages must record tick numbers and prices sequentially."""
        self.state.process_message(VersionMessage(version=1))
        self.state.process_message(TickMessage(version=1, tick_number=1, price=100.5))
        self.state.process_message(TickMessage(version=1, tick_number=2, price=101.25))

        self.assertEqual(self.state.ticks_received, 2)
        self.assertEqual(self.state.tick_numbers, [1, 2])
        self.assertEqual(self.state.prices, [100.5, 101.25])
        self.assertEqual(self.state.current_tick(), 2)

    def test_02_trade_messages_recorded(self) -> None:
        """TRADE messages must record trade ID, price, quantity, and order IDs."""
        self.state.process_message(VersionMessage(version=1))
        self.state.process_message(TickMessage(version=1, tick_number=1, price=100.0))
        self.state.process_message(
            TradeMessage(
                version=1,
                trade_id=42,
                buy_order_id=10,
                sell_order_id=20,
                price=100.0,
                quantity=15,
            )
        )
        self.assertEqual(self.state.trades_received, 1)

    def test_03_trade_buy_sell_classification(self) -> None:
        """Infers BUY when position increases and SELL when position decreases."""
        self.state.process_message(VersionMessage(version=1))
        # Initial portfolio position: 0
        self.state.process_message(
            PortfolioMessage(
                version=1,
                cash=10000.0,
                position_quantity=0,
                average_entry_price=0.0,
                realized_pnl=0.0,
                unrealized_pnl=0.0,
                total_pnl=0.0,
            )
        )
        self.state.process_message(TickMessage(version=1, tick_number=1, price=100.0))

        # 1. Buy trade: 25 units executed, position increases from 0 to 25
        self.state.process_message(
            TradeMessage(version=1, trade_id=1, buy_order_id=101, sell_order_id=102, price=100.0, quantity=25)
        )
        self.state.process_message(
            PortfolioMessage(
                version=1,
                cash=7500.0,
                position_quantity=25,
                average_entry_price=100.0,
                realized_pnl=0.0,
                unrealized_pnl=0.0,
                total_pnl=0.0,
            )
        )

        self.assertEqual(len(self.state.trades), 1)
        self.assertEqual(self.state.trades[0].side, InferredSide.BUY)
        self.assertEqual(self.state.trades[0].quantity, 25)

        # 2. Sell trade: 10 units executed, position decreases from 25 to 15
        self.state.process_message(TickMessage(version=1, tick_number=2, price=105.0))
        self.state.process_message(
            TradeMessage(version=1, trade_id=2, buy_order_id=103, sell_order_id=104, price=105.0, quantity=10)
        )
        self.state.process_message(
            PortfolioMessage(
                version=1,
                cash=8550.0,
                position_quantity=15,
                average_entry_price=100.0,
                realized_pnl=50.0,
                unrealized_pnl=75.0,
                total_pnl=125.0,
            )
        )

        self.assertEqual(len(self.state.trades), 2)
        self.assertEqual(self.state.trades[1].side, InferredSide.SELL)
        self.assertEqual(self.state.trades[1].quantity, 10)

        # Check filter helpers
        self.assertEqual(len(self.state.get_buy_trades()), 1)
        self.assertEqual(len(self.state.get_sell_trades()), 1)

    def test_04_unclassifiable_trades_not_invented(self) -> None:
        """Trades with no portfolio position change must be marked UNCLASSIFIED."""
        self.state.process_message(VersionMessage(version=1))
        # Initial position: 10
        self.state.process_message(
            PortfolioMessage(
                version=1,
                cash=5000.0,
                position_quantity=10,
                average_entry_price=50.0,
                realized_pnl=0.0,
                unrealized_pnl=0.0,
                total_pnl=0.0,
            )
        )
        self.state.process_message(TickMessage(version=1, tick_number=1, price=50.0))

        # External trade between two market participants (portfolio position unchanged at 10)
        self.state.process_message(
            TradeMessage(version=1, trade_id=99, buy_order_id=201, sell_order_id=202, price=50.0, quantity=5)
        )
        self.state.process_message(
            PortfolioMessage(
                version=1,
                cash=5000.0,
                position_quantity=10,
                average_entry_price=50.0,
                realized_pnl=0.0,
                unrealized_pnl=0.0,
                total_pnl=0.0,
            )
        )

        self.assertEqual(len(self.state.trades), 1)
        self.assertEqual(self.state.trades[0].side, InferredSide.UNCLASSIFIED)
        self.assertEqual(len(self.state.get_unclassified_trades()), 1)
        self.assertEqual(len(self.state.get_buy_trades()), 0)
        self.assertEqual(len(self.state.get_sell_trades()), 0)

        # Trade followed directly by END without portfolio update also remains UNCLASSIFIED
        self.state.process_message(
            TradeMessage(version=1, trade_id=100, buy_order_id=301, sell_order_id=302, price=50.0, quantity=2)
        )
        self.state.process_message(EndMessage(version=1))
        self.assertEqual(len(self.state.trades), 2)
        self.assertEqual(self.state.trades[1].side, InferredSide.UNCLASSIFIED)

    def test_05_portfolio_updates_every_metric(self) -> None:
        """PORTFOLIO messages must update every displayed portfolio metric accurately."""
        self.state.process_message(VersionMessage(version=1))
        p_msg = PortfolioMessage(
            version=1,
            cash=12345.67,
            position_quantity=45,
            average_entry_price=98.50,
            realized_pnl=150.25,
            unrealized_pnl=-35.50,
            total_pnl=114.75,
        )
        self.state.process_message(p_msg)

        self.assertIsNotNone(self.state.latest_portfolio)
        self.assertEqual(self.state.latest_portfolio.cash, 12345.67)
        self.assertEqual(self.state.latest_portfolio.position_quantity, 45)
        self.assertEqual(self.state.latest_portfolio.average_entry_price, 98.50)
        self.assertEqual(self.state.latest_portfolio.realized_pnl, 150.25)
        self.assertEqual(self.state.latest_portfolio.unrealized_pnl, -35.50)
        self.assertEqual(self.state.latest_portfolio.total_pnl, 114.75)

    def test_06_pnl_history_updates(self) -> None:
        """P&L history tracks total, realized, and unrealized values over time."""
        self.state.process_message(VersionMessage(version=1))
        self.state.process_message(TickMessage(version=1, tick_number=1, price=100.0))
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=10000.0, position_quantity=0, average_entry_price=0.0,
                realized_pnl=0.0, unrealized_pnl=0.0, total_pnl=0.0
            )
        )
        self.state.process_message(TickMessage(version=1, tick_number=2, price=102.0))
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=8000.0, position_quantity=20, average_entry_price=100.0,
                realized_pnl=0.0, unrealized_pnl=40.0, total_pnl=40.0
            )
        )

        self.assertEqual(self.state.pnl_ticks, [1, 2])
        self.assertEqual(self.state.total_pnl_history, [0.0, 40.0])
        self.assertEqual(self.state.realized_pnl_history, [0.0, 0.0])
        self.assertEqual(self.state.unrealized_pnl_history, [0.0, 40.0])

    def test_06b_pnl_history_tracks_actual_ticks_and_duplicate_ticks(self) -> None:
        """P&L snapshots record actual market tick numbers and handle multiple updates at same tick."""
        self.state.process_message(VersionMessage(version=1))
        # Initial snapshot before any ticks (tick 0)
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=10000.0, position_quantity=0, average_entry_price=0.0,
                realized_pnl=0.0, unrealized_pnl=0.0, total_pnl=0.0
            )
        )
        # Advance ticks to 10
        for i in range(1, 11):
            self.state.process_message(TickMessage(version=1, tick_number=i, price=100.0 + i))

        # First trade / portfolio update occurs at tick 10
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=9000.0, position_quantity=10, average_entry_price=110.0,
                realized_pnl=0.0, unrealized_pnl=0.0, total_pnl=0.0
            )
        )

        # A second portfolio update arrives at the exact same tick 10 without advancing tick
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=9000.0, position_quantity=10, average_entry_price=110.0,
                realized_pnl=0.0, unrealized_pnl=25.0, total_pnl=25.0
            )
        )

        # Advance ticks to 50
        for i in range(11, 51):
            self.state.process_message(TickMessage(version=1, tick_number=i, price=100.0 + i))

        # Third portfolio update occurs at tick 50
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=10500.0, position_quantity=0, average_entry_price=0.0,
                realized_pnl=500.0, unrealized_pnl=0.0, total_pnl=500.0
            )
        )

        # Verify tick numbers are actual market ticks (not list indices [0, 1, 2, 3])
        self.assertEqual(self.state.pnl_ticks, [0, 10, 10, 50])
        self.assertEqual(self.state.total_pnl_history, [0.0, 0.0, 25.0, 500.0])
        self.assertEqual(self.state.realized_pnl_history, [0.0, 0.0, 0.0, 500.0])
        self.assertEqual(self.state.unrealized_pnl_history, [0.0, 0.0, 25.0, 0.0])

    def test_07_streams_with_no_trades(self) -> None:
        """Streams with ticks but no trades must complete gracefully."""
        self.state.process_message(VersionMessage(version=1))
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=10000.0, position_quantity=0, average_entry_price=0.0,
                realized_pnl=0.0, unrealized_pnl=0.0, total_pnl=0.0
            )
        )
        for i in range(1, 6):
            self.state.process_message(TickMessage(version=1, tick_number=i, price=100.0 + i))
        self.state.process_message(EndMessage(version=1))

        self.assertEqual(self.state.ticks_received, 5)
        self.assertEqual(self.state.trades_received, 0)
        self.assertEqual(len(self.state.trades), 0)
        self.assertEqual(self.state.status, EngineStatus.COMPLETED)
        self.assertTrue(self.state.producer_completed_successfully)

    def test_08_empty_or_very_short_streams(self) -> None:
        """Immediate END after VERSION (0 ticks) must complete safely."""
        self.state.process_message(VersionMessage(version=1))
        self.state.process_message(EndMessage(version=1))

        self.assertEqual(self.state.ticks_received, 0)
        self.assertEqual(self.state.trades_received, 0)
        self.assertEqual(self.state.status, EngineStatus.COMPLETED)
        self.assertTrue(self.state.producer_completed_successfully)

    def test_09_end_produces_completed_state(self) -> None:
        """END message transitions status to COMPLETED."""
        self.assertEqual(self.state.status, EngineStatus.WAITING)
        self.state.process_message(VersionMessage(version=1))
        self.assertEqual(self.state.status, EngineStatus.RUNNING)
        self.state.process_message(EndMessage(version=1))
        self.assertEqual(self.state.status, EngineStatus.COMPLETED)
        self.assertTrue(self.state.producer_completed_successfully)

    def test_10_error_state_handling(self) -> None:
        """set_error transitions state to ERROR with descriptive diagnostic message."""
        self.state.set_error("Producer aborted with exit code 1")
        self.assertEqual(self.state.status, EngineStatus.ERROR)
        self.assertIn("exit code 1", self.state.error_message)


class TestDashboardHeadlessRendering(unittest.TestCase):
    """Smoke tests for Matplotlib figure creation and frame rendering in headless mode."""

    def setUp(self) -> None:
        self.state = DashboardState()
        self.dashboard = TradingDashboard(state=self.state, interactive=False)

    def tearDown(self) -> None:
        plt.close(self.dashboard.fig)

    def test_11_headless_render_smoke_test(self) -> None:
        """Renders frames through Matplotlib non-interactive Agg backend without error."""
        # 1. Initial state frame
        self.dashboard.update_frame(0)

        # 2. Feed simulated stream into state
        self.state.process_message(VersionMessage(version=1))
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=10000.0, position_quantity=0, average_entry_price=0.0,
                realized_pnl=0.0, unrealized_pnl=0.0, total_pnl=0.0
            )
        )
        self.state.process_message(TickMessage(version=1, tick_number=1, price=100.25))
        self.state.process_message(TickMessage(version=1, tick_number=2, price=101.50))
        self.state.process_message(
            TradeMessage(version=1, trade_id=1, buy_order_id=10, sell_order_id=20, price=101.50, quantity=30)
        )
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=6955.0, position_quantity=30, average_entry_price=101.50,
                realized_pnl=0.0, unrealized_pnl=0.0, total_pnl=0.0
            )
        )
        self.state.process_message(EndMessage(version=1))

        # 3. Update frame with populated state
        self.dashboard.update_frame(1)

        # 4. Draw canvas to verify no graphics errors
        self.dashboard.fig.canvas.draw()

        # Check line data populated
        x_data, y_data = self.dashboard.line_price.get_data()
        self.assertEqual(list(x_data), [1, 2])
        self.assertEqual(list(y_data), [100.25, 101.50])
        self.assertIn("PORTFOLIO METRICS", self.dashboard.text_metrics.get_text())
        self.assertIn("COMPLETED", self.dashboard.text_status.get_text())

    def test_13_dashboard_updates_while_stream_running(self) -> None:
        """Verifies dashboard updates incrementally on each frame while stream is still running."""
        # Frame 0: empty
        self.dashboard.update_frame(0)
        x_data, _ = self.dashboard.line_price.get_data()
        self.assertEqual(len(x_data), 0)

        # Enqueue Version & Tick 1 (stream running, no END)
        self.dashboard.event_queue.put(("MESSAGE", VersionMessage(version=1)))
        self.dashboard.event_queue.put(("MESSAGE", TickMessage(version=1, tick_number=1, price=100.0)))
        self.dashboard.update_frame(1)

        x_data, y_data = self.dashboard.line_price.get_data()
        self.assertEqual(list(x_data), [1])
        self.assertEqual(list(y_data), [100.0])
        self.assertEqual(self.state.status, EngineStatus.RUNNING)
        self.assertFalse(self.state.producer_completed_successfully)
        self.assertIn("RUNNING", self.dashboard.text_status.get_text())

        # Enqueue Tick 2 (stream still running, no END)
        self.dashboard.event_queue.put(("MESSAGE", TickMessage(version=1, tick_number=2, price=102.5)))
        self.dashboard.update_frame(2)

        x_data, y_data = self.dashboard.line_price.get_data()
        self.assertEqual(list(x_data), [1, 2])
        self.assertEqual(list(y_data), [100.0, 102.5])
        self.assertEqual(self.state.status, EngineStatus.RUNNING)
        self.assertFalse(self.state.producer_completed_successfully)

    def test_14_pnl_chart_uses_actual_market_ticks(self) -> None:
        """Verifies that P&L chart plots against actual market tick numbers and shares x-axis with price chart."""
        self.state.process_message(VersionMessage(version=1))
        # Initial portfolio update before any ticks (tick 0)
        self.state.process_message(
            PortfolioMessage(
                version=1, cash=10000.0, position_quantity=0, average_entry_price=0.0,
                realized_pnl=0.0, unrealized_pnl=0.0, total_pnl=0.0
            )
        )
        # Advance ticks to 50
        for i in range(1, 51):
            self.state.process_message(TickMessage(version=1, tick_number=i, price=100.0 + i))
            if i == 15:
                # Portfolio update at tick 15
                self.state.process_message(
                    PortfolioMessage(
                        version=1, cash=8500.0, position_quantity=15, average_entry_price=100.0,
                        realized_pnl=0.0, unrealized_pnl=225.0, total_pnl=225.0
                    )
                )
            elif i == 30:
                # Multiple portfolio updates at the exact same tick 30
                self.state.process_message(
                    PortfolioMessage(
                        version=1, cash=7000.0, position_quantity=30, average_entry_price=105.0,
                        realized_pnl=50.0, unrealized_pnl=700.0, total_pnl=750.0
                    )
                )
                self.state.process_message(
                    PortfolioMessage(
                        version=1, cash=7000.0, position_quantity=30, average_entry_price=105.0,
                        realized_pnl=50.0, unrealized_pnl=750.0, total_pnl=800.0
                    )
                )

        self.assertEqual(self.state.pnl_ticks, [0, 15, 30, 30])

        self.dashboard.update_frame(0)
        self.dashboard.fig.canvas.draw()

        # P&L line series must have exact market tick numbers as x-coordinates
        x_total, y_total = self.dashboard.line_total_pnl.get_data()
        x_real, y_real = self.dashboard.line_realized_pnl.get_data()
        x_unreal, y_unreal = self.dashboard.line_unrealized_pnl.get_data()

        self.assertEqual(list(x_total), [0, 15, 30, 30])
        self.assertEqual(list(x_real), [0, 15, 30, 30])
        self.assertEqual(list(x_unreal), [0, 15, 30, 30])

        # ax_pnl must share x-axis with ax_price
        self.assertTrue(self.dashboard.ax_pnl.get_shared_x_axes().joined(self.dashboard.ax_pnl, self.dashboard.ax_price))
        # Both charts must share identical x-limits spanning the market ticks
        self.assertEqual(self.dashboard.ax_pnl.get_xlim(), self.dashboard.ax_price.get_xlim())


class TestDashboardEndToEnd(unittest.TestCase):
    """End-to-end integration test with the compiled C++ producer binary."""

    def test_12_end_to_end_real_producer(self) -> None:
        """Executes real C++ producer through OS pipe into dashboard state."""
        producer_bin = project_root / "bin" / "ipc_producer"
        if not producer_bin.exists():
            producer_bin = project_root / "build" / "ipc_producer"

        self.assertTrue(producer_bin.exists(), f"C++ producer not found at {producer_bin}")

        launcher = IPCLauncher(
            executable_path=str(producer_bin),
            args=["--ticks", "10", "--seed", "42"],
        )
        state = DashboardState()
        dashboard = TradingDashboard(launcher=launcher, state=state, interactive=False)

        # Start background stream and await completion
        dashboard.start_background_stream()
        if dashboard.worker_thread:
            dashboard.worker_thread.join(timeout=5.0)

        # Drain all remaining events and update layout
        dashboard.update_frame(0)
        plt.close(dashboard.fig)

        # Verify state results match simulation
        self.assertEqual(state.status, EngineStatus.COMPLETED)
        self.assertTrue(state.producer_completed_successfully)
        self.assertEqual(state.ticks_received, 10)
        self.assertEqual(state.trades_received, 2)
        self.assertEqual(len(state.trades), 2)
        self.assertEqual(state.trades[0].side, InferredSide.BUY)
        self.assertEqual(state.trades[1].side, InferredSide.SELL)
        self.assertIsNotNone(state.latest_portfolio)
        self.assertGreater(len(state.pnl_ticks), 0)
        self.assertEqual(len(state.pnl_ticks), 11)
        self.assertEqual(state.pnl_ticks, list(range(11)))

    def test_15_end_to_end_50_ticks_continuous_pnl_updates(self) -> None:
        """Verifies a 50-tick run produces portfolio snapshots throughout the run and unrealized P&L marks to market."""
        producer_bin = project_root / "bin" / "ipc_producer"
        if not producer_bin.exists():
            producer_bin = project_root / "build" / "ipc_producer"

        self.assertTrue(producer_bin.exists(), f"C++ producer not found at {producer_bin}")

        launcher = IPCLauncher(
            executable_path=str(producer_bin),
            args=["--ticks", "50", "--seed", "42"],
        )
        state = DashboardState()
        dashboard = TradingDashboard(launcher=launcher, state=state, interactive=False)

        # Execute stream
        launcher.run(on_message=dashboard.state.process_message)
        dashboard.update_frame(0)
        dashboard.fig.canvas.draw()

        # 1. Full 50 ticks received
        self.assertEqual(state.ticks_received, 50)
        self.assertEqual(len(state.tick_numbers), 50)

        # 2. Portfolio snapshots emitted throughout simulation (initial tick 0 + all ticks 1..50)
        self.assertEqual(len(state.pnl_ticks), 51)
        self.assertEqual(state.pnl_ticks, list(range(51)))
        self.assertEqual(min(state.pnl_ticks), 0)
        self.assertEqual(max(state.pnl_ticks), 50)

        # 3. Both trades executed and correctly classified
        self.assertEqual(state.trades_received, 2)
        self.assertEqual(len(state.trades), 2)
        self.assertEqual(state.trades[0].side, InferredSide.BUY)
        self.assertEqual(state.trades[1].side, InferredSide.SELL)

        # 4. Open position exists after tick 3 (bought 30) and after tick 7 (sold 10 -> holds 20)
        self.assertEqual(state.latest_portfolio.position_quantity, 20)
        self.assertAlmostEqual(state.latest_portfolio.realized_pnl, -3.980933, places=4)

        # 5. Unrealized P&L actively changes with market price when position is open
        post_trade_unrealized = state.unrealized_pnl_history[8:]  # ticks 8 to 50
        self.assertGreater(
            len(set(round(p, 4) for p in post_trade_unrealized)),
            10,
            "Unrealized P&L should fluctuate dynamically with market price across ticks",
        )

        # Spot-check mark-to-market formula: unrealized_pnl == position * (price - avg_entry_price)
        avg_entry = state.latest_portfolio.average_entry_price
        for tick_idx in range(8, 51):
            expected_unrealized = 20 * (state.prices[tick_idx - 1] - avg_entry)
            self.assertAlmostEqual(state.unrealized_pnl_history[tick_idx], expected_unrealized, places=4)

        # 6. Dashboard P&L line chart spans all 50 market ticks
        x_pnl, _ = dashboard.line_total_pnl.get_data()
        self.assertEqual(list(x_pnl), list(range(51)))
        self.assertEqual(dashboard.ax_pnl.get_xlim(), dashboard.ax_price.get_xlim())

        plt.close(dashboard.fig)


if __name__ == "__main__":
    unittest.main()
