"""Module 10: End-to-End Integration and Reliability Test Suite.

Validates:
  1. End-to-end simulation correctness (seeded reproducibility, protocol lifecycle,
     mark-to-market unrealized P&L, trade cash accounting, risk manager enforcement,
     event ordering, dashboard state fidelity).
  2. Failure modes and edge cases (invalid producer CLI args, missing binary,
     abnormal producer exit, empty/short runs, missing VERSION/END, malformed records,
     benchmark argument validation, and CSV writing failures).
  3. Process and resource reliability (stdout isolation, stderr draining,
     subprocess termination and zombie reaping, dashboard window closure cleanup,
     headless smoke tests, and error exit code propagation).
"""

import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

# Ensure project directories are in sys.path
project_root = Path(__file__).resolve().parent.parent
visualizer_dir = project_root / "visualizer"
if str(visualizer_dir) not in sys.path:
    sys.path.insert(0, str(visualizer_dir))

from dashboard_state import DashboardState, EngineStatus, InferredSide
from dashboard import TradingDashboard
from ipc_consumer import (
    DEFAULT_PROTOCOL_VERSION,
    EndMessage,
    IPCLauncher,
    IPCProtocolConsumer,
    PortfolioMessage,
    ProducerError,
    ProtocolError,
    TickMessage,
    TradeMessage,
    UnexpectedEOFError,
    VersionMessage,
)


def get_producer_bin() -> Path:
    """Returns the path to the compiled C++ ipc_producer binary."""
    p1 = project_root / "bin" / "ipc_producer"
    if p1.is_file() and os.access(p1, os.X_OK):
        return p1
    p2 = project_root / "build" / "ipc_producer"
    if p2.is_file() and os.access(p2, os.X_OK):
        return p2
    raise FileNotFoundError(f"C++ producer binary not found at {p1} or {p2}. Please compile it first.")


def get_benchmark_bin(name: str) -> Path:
    """Returns the path to a compiled benchmark executable."""
    p = project_root / "bin" / name
    if p.is_file() and os.access(p, os.X_OK):
        return p
    raise FileNotFoundError(f"Benchmark binary not found at {p}. Please compile it first.")


class TestSimulationCorrectness(unittest.TestCase):
    """Verifies correctness of the real C++ simulation engine and IPC stream."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.producer_path = str(get_producer_bin())

    def test_seeded_reproducibility(self) -> None:
        """A seeded run must be 100% reproducible across ticks and execution outcomes."""
        launcher1 = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "25", "--seed", "42"])
        res1 = launcher1.run()

        launcher2 = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "25", "--seed", "42"])
        res2 = launcher2.run()

        # Both runs must have identical exit codes and message counts
        self.assertEqual(res1["exit_code"], 0)
        self.assertEqual(res2["exit_code"], 0)
        self.assertEqual(res1["counts"], res2["counts"])
        self.assertEqual(len(res1["messages"]), len(res2["messages"]))

        # Every single message must match identically
        for m1, m2 in zip(res1["messages"], res2["messages"]):
            self.assertEqual(type(m1), type(m2))
            if isinstance(m1, TickMessage):
                self.assertEqual(m1.tick_number, m2.tick_number)
                self.assertAlmostEqual(m1.price, m2.price, places=6)
            elif isinstance(m1, TradeMessage):
                self.assertEqual(m1.trade_id, m2.trade_id)
                self.assertEqual(m1.quantity, m2.quantity)
                self.assertAlmostEqual(m1.price, m2.price, places=6)
            elif isinstance(m1, PortfolioMessage):
                self.assertEqual(m1.position_quantity, m2.position_quantity)
                self.assertAlmostEqual(m1.cash, m2.cash, places=5)
                self.assertAlmostEqual(m1.realized_pnl, m2.realized_pnl, places=5)
                self.assertAlmostEqual(m1.unrealized_pnl, m2.unrealized_pnl, places=5)

        # A different seed must produce a different price sequence
        launcher_diff = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "25", "--seed", "999"])
        res_diff = launcher_diff.run()
        ticks1 = [m.price for m in res1["messages"] if isinstance(m, TickMessage)]
        ticks_diff = [m.price for m in res_diff["messages"] if isinstance(m, TickMessage)]
        self.assertNotEqual(ticks1, ticks_diff)

    def test_protocol_lifecycle(self) -> None:
        """A successful simulation run must emit VERSION first, followed by initial PORTFOLIO, then ticks, and END last."""
        launcher = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "10", "--seed", "42"])
        res = launcher.run()
        messages = res["messages"]

        self.assertGreater(len(messages), 4)
        # Message 0 must be VersionMessage
        self.assertIsInstance(messages[0], VersionMessage)
        self.assertEqual(messages[0].version, DEFAULT_PROTOCOL_VERSION)

        # Message 1 must be initial PortfolioMessage
        self.assertIsInstance(messages[1], PortfolioMessage)
        self.assertEqual(messages[1].position_quantity, 0)
        self.assertAlmostEqual(messages[1].cash, 10000.0, places=2)

        # Message -1 must be EndMessage
        self.assertIsInstance(messages[-1], EndMessage)
        self.assertEqual(messages[-1].version, DEFAULT_PROTOCOL_VERSION)

    def test_mark_to_market_unrealized_pnl(self) -> None:
        """Portfolio snapshots must reflect mark-to-market unrealized P&L: position * (price - avg_entry_price)."""
        launcher = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "20", "--seed", "42"])
        res = launcher.run()

        latest_price: float = 100.0
        for msg in res["messages"]:
            if isinstance(msg, TickMessage):
                latest_price = msg.price
            elif isinstance(msg, PortfolioMessage):
                pos = msg.position_quantity
                avg_entry = msg.average_entry_price
                expected_unrealized = pos * (latest_price - avg_entry) if pos > 0 else 0.0
                self.assertAlmostEqual(
                    msg.unrealized_pnl,
                    expected_unrealized,
                    places=4,
                    msg=f"Mark-to-market mismatch: pos={pos}, price={latest_price}, avg={avg_entry}",
                )
                self.assertAlmostEqual(
                    msg.total_pnl,
                    msg.realized_pnl + msg.unrealized_pnl,
                    places=4,
                    msg="Total P&L must equal realized_pnl + unrealized_pnl",
                )

    def test_total_equity_conservation(self) -> None:
        """Total equity (cash + position * current_price) must equal starting_cash + total_pnl at all times."""
        starting_cash = 10000.0
        launcher = IPCLauncher(
            executable_path=self.producer_path,
            args=["--ticks", "20", "--seed", "42", "--cash", str(starting_cash)],
        )
        res = launcher.run()

        current_price = 100.0
        for msg in res["messages"]:
            if isinstance(msg, TickMessage):
                current_price = msg.price
            elif isinstance(msg, PortfolioMessage):
                total_equity = msg.cash + (msg.position_quantity * current_price)
                expected_equity = starting_cash + msg.total_pnl
                self.assertAlmostEqual(
                    total_equity,
                    expected_equity,
                    places=4,
                    msg=f"Equity conservation violated: cash={msg.cash}, pos={msg.position_quantity}, price={current_price}",
                )

    def test_realized_pnl_after_trades(self) -> None:
        """Realized P&L and position accounting must remain consistent after buy and sell trades."""
        launcher = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "10", "--seed", "42"])
        res = launcher.run()

        trades = [m for m in res["messages"] if isinstance(m, TradeMessage)]
        self.assertEqual(len(trades), 2)

        trade_buy = trades[0]
        trade_sell = trades[1]

        # Buy: 30 units at tick 3 price
        self.assertEqual(trade_buy.quantity, 30)
        # Sell: 10 units at tick 7 price
        self.assertEqual(trade_sell.quantity, 10)

        # Expected realized P&L after selling 10 units = 10 * (sell_price - buy_price)
        expected_realized = 10 * (trade_sell.price - trade_buy.price)

        final_portfolio = [m for m in res["messages"] if isinstance(m, PortfolioMessage)][-1]
        self.assertEqual(final_portfolio.position_quantity, 20)
        self.assertAlmostEqual(final_portfolio.average_entry_price, trade_buy.price, places=5)
        self.assertAlmostEqual(final_portfolio.realized_pnl, expected_realized, places=4)

    def test_risk_manager_order_rejection(self) -> None:
        """The risk manager must reject excessive orders when limits are configured."""
        # 1. Setting max-order-qty to 20 rejects the 30-unit buy order proposed on tick 3
        launcher_restricted = IPCLauncher(
            executable_path=self.producer_path,
            args=["--ticks", "10", "--seed", "42", "--max-order-qty", "20"],
        )
        res_restricted = launcher_restricted.run()

        # Zero trades should be executed
        self.assertEqual(res_restricted["counts"]["TRADE"], 0)
        # Position remains 0 units and cash remains unchanged across the entire run
        final_port = [m for m in res_restricted["messages"] if isinstance(m, PortfolioMessage)][-1]
        self.assertEqual(final_port.position_quantity, 0)
        self.assertAlmostEqual(final_port.cash, 10000.0, places=2)
        self.assertIn("Risk check rejected", res_restricted["stderr"])

        # 2. Standard run allows the 30-unit order
        launcher_standard = IPCLauncher(
            executable_path=self.producer_path,
            args=["--ticks", "10", "--seed", "42", "--max-order-qty", "200"],
        )
        res_standard = launcher_standard.run()
        self.assertEqual(res_standard["counts"]["TRADE"], 2)

    def test_event_order_integrity(self) -> None:
        """Trade and portfolio events must be processed in the expected sequence."""
        launcher = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "10", "--seed", "42"])
        res = launcher.run()

        last_tick_number = 0
        saw_version = False
        saw_end = False

        for msg in res["messages"]:
            if isinstance(msg, VersionMessage):
                self.assertFalse(saw_version, "Duplicate VERSION message")
                saw_version = True
            elif isinstance(msg, TickMessage):
                self.assertTrue(saw_version, "TICK before VERSION")
                self.assertFalse(saw_end, "TICK after END")
                self.assertEqual(msg.tick_number, last_tick_number + 1)
                last_tick_number = msg.tick_number
            elif isinstance(msg, TradeMessage):
                self.assertTrue(saw_version)
                self.assertFalse(saw_end)
                self.assertGreater(last_tick_number, 0, "TRADE before first TICK")
            elif isinstance(msg, PortfolioMessage):
                self.assertTrue(saw_version)
                self.assertFalse(saw_end)
            elif isinstance(msg, EndMessage):
                self.assertFalse(saw_end, "Duplicate END message")
                saw_end = True

        self.assertTrue(saw_version)
        self.assertTrue(saw_end)
        self.assertEqual(last_tick_number, 10)

    def test_dashboard_state_synchronization(self) -> None:
        """Dashboard's tick, trade, and portfolio histories must match events emitted by the producer."""
        launcher = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "15", "--seed", "42"])
        state = DashboardState()
        res = launcher.run(on_message=state.process_message)

        # Check state status
        self.assertEqual(state.status, EngineStatus.COMPLETED)
        self.assertTrue(state.producer_completed_successfully)
        self.assertEqual(state.ticks_received, 15)
        self.assertEqual(state.trades_received, 2)
        self.assertEqual(len(state.tick_numbers), 15)
        self.assertEqual(len(state.prices), 15)
        self.assertEqual(len(state.pnl_ticks), 16)  # Initial tick 0 + 15 tick updates

        # Verify state values against producer result
        raw_ticks = [m.price for m in res["messages"] if isinstance(m, TickMessage)]
        self.assertEqual(state.prices, raw_ticks)


class TestFailureAndEdgeCases(unittest.TestCase):
    """Tests failure scenarios, edge cases, and argument validation."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.producer_path = str(get_producer_bin())

    def test_invalid_producer_cli_arguments(self) -> None:
        """Producer must exit with non-zero status when passed invalid CLI arguments."""
        invalid_arg_sets = [
            ["--ticks", "-5"],
            ["--ticks", "not_a_number"],
            ["--seed", "-1"],
            ["--seed", "bad_seed"],
            ["--price", "-10.0"],
            ["--price", "0.0"],
            ["--cash", "-500.0"],
            ["--max-order-qty", "0"],
            ["--max-order-qty", "-10"],
            ["--unknown-flag"],
        ]

        for args in invalid_arg_sets:
            launcher = IPCLauncher(executable_path=self.producer_path, args=args)
            with self.assertRaises(ProducerError, msg=f"Expected failure for args: {args}") as ctx:
                launcher.run()
            self.assertNotEqual(ctx.exception.exit_code, 0)
            self.assertIn("Error", ctx.exception.stderr)

    def test_trailing_garbage_and_integer_overflow_rejection(self) -> None:
        """Producer must strictly reject inputs with trailing non-numeric garbage or integers exceeding limits."""
        cases = [
            (["--ticks", "10abc"], "Invalid --ticks value: 10abc"),
            (["--seed", "42xyz"], "Invalid --seed value: 42xyz"),
            (["--price", "100.5extra"], "Invalid --price value: 100.5extra"),
            (["--cash", "10000.0junk"], "Invalid --cash value: 10000.0junk"),
            (["--max-order-qty", "200bad"], "Invalid --max-order-qty value: 200bad"),
            (["--max-position", "500bad"], "Invalid --max-position value: 500bad"),
            (["--seed", "4294967296"], "--seed exceeds 32-bit limit"),
            (["--seed", "9999999999"], "--seed exceeds 32-bit limit"),
            (["--max-order-qty", "4294967296"], "--max-order-qty exceeds 32-bit limit"),
            (["--max-position", "4294967296"], "--max-position exceeds 32-bit limit"),
            (["--ticks", "999999999999999999999999999999"], "Invalid --ticks value"),
            (["--seed", "999999999999999999999999999999"], "Invalid --seed value"),
        ]

        for args, expected_err in cases:
            launcher = IPCLauncher(executable_path=self.producer_path, args=args)
            with self.assertRaises(ProducerError, msg=f"Expected failure for args: {args}") as ctx:
                launcher.run()
            self.assertNotEqual(ctx.exception.exit_code, 0)
            self.assertIn("Error", ctx.exception.stderr)
            self.assertIn(expected_err, ctx.exception.stderr)

    def test_missing_producer_executable(self) -> None:
        """Launcher must raise FileNotFoundError when executable path does not exist."""
        launcher = IPCLauncher(executable_path="/nonexistent/path/to/producer")
        with self.assertRaises(FileNotFoundError):
            launcher.run()

    def test_producer_abnormal_exit(self) -> None:
        """Producer failure (--fail-for-test) must cleanly propagate non-zero exit code and stderr."""
        launcher = IPCLauncher(executable_path=self.producer_path, args=["--fail-for-test"])
        with self.assertRaises(ProducerError) as ctx:
            launcher.run()
        self.assertEqual(ctx.exception.exit_code, 42)
        self.assertIn("Intentional failure triggered", ctx.exception.stderr)

    def test_empty_simulation_zero_ticks(self) -> None:
        """Producer with --ticks 0 must cleanly emit VERSION, initial PORTFOLIO, and END without crashes."""
        launcher = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "0", "--seed", "42"])
        res = launcher.run()

        self.assertEqual(res["exit_code"], 0)
        self.assertEqual(res["counts"]["VERSION"], 1)
        self.assertEqual(res["counts"]["TICK"], 0)
        self.assertEqual(res["counts"]["TRADE"], 0)
        self.assertEqual(res["counts"]["PORTFOLIO"], 1)
        self.assertEqual(res["counts"]["END"], 1)

    def test_short_simulation_one_and_two_ticks(self) -> None:
        """Short runs with 1 and 2 ticks (before any scheduled trades) must complete cleanly."""
        for count in (1, 2):
            launcher = IPCLauncher(executable_path=self.producer_path, args=["--ticks", str(count), "--seed", "42"])
            res = launcher.run()
            self.assertEqual(res["exit_code"], 0)
            self.assertEqual(res["counts"]["TICK"], count)
            self.assertEqual(res["counts"]["TRADE"], 0)
            self.assertEqual(res["counts"]["PORTFOLIO"], count + 1)
            self.assertEqual(res["counts"]["END"], 1)

    def test_protocol_violation_missing_version(self) -> None:
        """A stream that starts with TICK before VERSION must raise ProtocolError."""
        consumer = IPCProtocolConsumer()
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t1\t100.5\n")

    def test_protocol_violation_missing_end(self) -> None:
        """A stream that terminates unexpectedly before END must raise UnexpectedEOFError."""
        consumer = IPCProtocolConsumer()
        stream = ["VERSION\t1\n", "TICK\t1\t1\t100.5\n"]
        with self.assertRaises(UnexpectedEOFError):
            list(consumer.consume_stream(stream))

    def test_protocol_violation_malformed_messages(self) -> None:
        """Malformed messages must be rejected strictly."""
        consumer = IPCProtocolConsumer()
        consumer.parse_line("VERSION\t1\n")

        # Wrong token count
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t1\n")
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t1\t100.0\textra\n")

        # Non-finite float
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t2\tnan\n")
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t2\tinf\n")

        # Non-positive price
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t2\t0.00\n")
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t2\t-10.00\n")

        # Zero tick number
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t0\t100.00\n")

        # Message after END
        consumer.parse_line("END\t1\n")
        with self.assertRaises(ProtocolError):
            consumer.parse_line("TICK\t1\t3\t101.00\n")

    def test_benchmark_invalid_arguments(self) -> None:
        """Benchmark executables must exit with code 1 when passed invalid arguments."""
        md_bin = str(get_benchmark_bin("benchmark_market_data"))
        oe_bin = str(get_benchmark_bin("benchmark_order_execution"))

        # Market data benchmark invalid inputs
        p1 = subprocess.run([md_bin, "--ticks", "-5"], capture_output=True, text=True)
        self.assertEqual(p1.returncode, 1)

        p2 = subprocess.run([md_bin, "--ticks", "bad_value"], capture_output=True, text=True)
        self.assertEqual(p2.returncode, 1)

        # Order execution benchmark invalid inputs
        p3 = subprocess.run([oe_bin, "--ops", "-10"], capture_output=True, text=True)
        self.assertEqual(p3.returncode, 1)

        p4 = subprocess.run([oe_bin, "--ops", "bad_value"], capture_output=True, text=True)
        self.assertEqual(p4.returncode, 1)

    def test_benchmark_csv_writing_failure(self) -> None:
        """Benchmark executables must fail cleanly when output directory is unwritable."""
        md_bin = str(get_benchmark_bin("benchmark_market_data"))
        oe_bin = str(get_benchmark_bin("benchmark_order_execution"))

        # Target an unwritable path under /dev/null
        impossible_csv = "/dev/null/impossible_dir/output.csv"

        p_md = subprocess.run(
            [md_bin, "--ticks", "100", "--latency-samples", "10", "--output", impossible_csv],
            capture_output=True,
            text=True,
        )
        self.assertEqual(p_md.returncode, 1)
        self.assertIn("Failed to write CSV file", p_md.stderr)

        p_oe = subprocess.run(
            [oe_bin, "--ops", "100", "--latency-samples", "10", "--output", impossible_csv],
            capture_output=True,
            text=True,
        )
        self.assertEqual(p_oe.returncode, 1)
        self.assertIn("Failed to write CSV file", p_oe.stderr)


class TestProcessAndResourceReliability(unittest.TestCase):
    """Verifies pipe isolation, deadlock prevention, subprocess cleanup, and GUI lifecycle."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.producer_path = str(get_producer_bin())

    def test_stdout_isolation_and_stderr_drain(self) -> None:
        """Standard output must contain only protocol records while stderr captures diagnostics without deadlock."""
        launcher = IPCLauncher(executable_path=self.producer_path, args=["--ticks", "20", "--seed", "42"])
        res = launcher.run()

        # Check stderr
        self.assertTrue(len(res["stderr"]) > 0)
        self.assertIn("[IPC Producer] Starting simulation stream", res["stderr"])
        self.assertIn("Finished simulation successfully", res["stderr"])

        # Check stdout records: only valid message types
        for msg in res["messages"]:
            self.assertIn(type(msg).__name__, ("VersionMessage", "TickMessage", "TradeMessage", "PortfolioMessage", "EndMessage"))

    def test_subprocess_cleanup_on_early_termination(self) -> None:
        """Terminating an active IPCLauncher must immediately reap the child process without orphans."""
        launcher = IPCLauncher(
            executable_path=self.producer_path,
            args=["--ticks", "500000", "--seed", "42"],
        )

        import threading

        def run_target():
            try:
                launcher.run()
            except Exception:
                pass

        t = threading.Thread(target=run_target, daemon=True)
        t.start()

        # Wait until process is spawned
        import time
        for _ in range(50):
            if launcher.process is not None and launcher.process.poll() is None:
                break
            time.sleep(0.01)

        self.assertIsNotNone(launcher.process)
        proc = launcher.process

        # Terminate launcher
        launcher.terminate()
        t.join(timeout=2.0)

        # Process must be reaped (poll() is not None, returncode is not None)
        self.assertIsNotNone(proc.poll())
        self.assertIsNotNone(proc.returncode)

    def test_headless_dashboard_smoke(self) -> None:
        """Headless dashboard must run with real producer, render, and save PNG cleanly."""
        out_file = project_root / "build" / "test_smoke_dashboard.png"
        if out_file.exists():
            out_file.unlink()

        cmd = [
            sys.executable,
            str(visualizer_dir / "dashboard.py"),
            "--headless",
            "--producer-bin", self.producer_path,
            "--ticks", "10",
            "--seed", "42",
            "--output", str(out_file),
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, f"Headless dashboard failed: {proc.stderr}")
        self.assertTrue(out_file.exists(), "Dashboard PNG was not generated")
        self.assertGreater(out_file.stat().st_size, 1000, "Dashboard PNG is empty")

        # Cleanup output file
        out_file.unlink()

    def test_headless_dashboard_error_exit_code(self) -> None:
        """Headless dashboard must return non-zero exit code when the producer fails to launch."""
        cmd = [
            sys.executable,
            str(visualizer_dir / "dashboard.py"),
            "--headless",
            "--producer-bin", "/nonexistent/binary/path",
            "--output", str(project_root / "build" / "dummy.png"),
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("not found", proc.stderr)


if __name__ == "__main__":
    unittest.main()
