"""Unit tests for the C++ <-> Python IPC Protocol Consumer and Launcher.

Covers:
  1. Valid serialization and parsing for every message type.
  2. Correct field order and protocol version.
  3. Missing fields and extra fields rejection.
  4. Invalid numeric values and non-finite floats (NaN, Inf, -Inf).
  5. Unknown message types and unsupported protocol versions.
  6. Missing or duplicate VERSION announcements.
  7. Missing END and unexpected EOF detection.
  8. Messages received after END rejection.
  9. Blank-line behavior (default skip vs. strict error).
 10. Successful end-to-end execution of the C++ producer through the Python launcher.
 11. Non-zero producer exit handling and stderr capture.
"""

from pathlib import Path
import subprocess
import sys
import unittest

# Ensure visualizer is on path
project_root = Path(__file__).resolve().parent.parent
visualizer_dir = project_root / "visualizer"
if str(visualizer_dir) not in sys.path:
    sys.path.insert(0, str(visualizer_dir))

from ipc_consumer import (  # noqa: E402
    IPCLauncher,
    IPCProtocolConsumer,
    ProtocolError,
    ProducerError,
    UnexpectedEOFError,
    VersionMessage,
    TickMessage,
    TradeMessage,
    PortfolioMessage,
    EndMessage,
)


class TestIPCConsumer(unittest.TestCase):
    def setUp(self) -> None:
        self.consumer = IPCProtocolConsumer()

    # -------------------------------------------------------------------------
    # 1. Valid serialization and parsing for every message type
    # -------------------------------------------------------------------------
    def test_valid_version_message(self) -> None:
        msg = self.consumer.parse_line("VERSION\t1\n")
        self.assertIsInstance(msg, VersionMessage)
        self.assertEqual(msg.version, 1)

    def test_valid_tick_message(self) -> None:
        self.consumer.parse_line("VERSION\t1")
        msg = self.consumer.parse_line("TICK\t1\t42\t105.750000")
        self.assertIsInstance(msg, TickMessage)
        self.assertEqual(msg.version, 1)
        self.assertEqual(msg.tick_number, 42)
        self.assertAlmostEqual(msg.price, 105.75, places=5)

    def test_valid_trade_message(self) -> None:
        self.consumer.parse_line("VERSION\t1")
        msg = self.consumer.parse_line("TRADE\t1\t10\t101\t102\t99.500000\t25")
        self.assertIsInstance(msg, TradeMessage)
        self.assertEqual(msg.version, 1)
        self.assertEqual(msg.trade_id, 10)
        self.assertEqual(msg.buy_order_id, 101)
        self.assertEqual(msg.sell_order_id, 102)
        self.assertAlmostEqual(msg.price, 99.50, places=5)
        self.assertEqual(msg.quantity, 25)

    def test_valid_portfolio_message(self) -> None:
        self.consumer.parse_line("VERSION\t1")
        msg = self.consumer.parse_line("PORTFOLIO\t1\t9500.500000\t50\t100.250000\t12.500000\t-5.000000\t7.500000")
        self.assertIsInstance(msg, PortfolioMessage)
        self.assertEqual(msg.version, 1)
        self.assertAlmostEqual(msg.cash, 9500.50, places=5)
        self.assertEqual(msg.position_quantity, 50)
        self.assertAlmostEqual(msg.average_entry_price, 100.25, places=5)
        self.assertAlmostEqual(msg.realized_pnl, 12.50, places=5)
        self.assertAlmostEqual(msg.unrealized_pnl, -5.00, places=5)
        self.assertAlmostEqual(msg.total_pnl, 7.50, places=5)

    def test_valid_end_message(self) -> None:
        self.consumer.parse_line("VERSION\t1")
        msg = self.consumer.parse_line("END\t1")
        self.assertIsInstance(msg, EndMessage)
        self.assertEqual(msg.version, 1)

    # -------------------------------------------------------------------------
    # 2. Correct field order and protocol version
    # -------------------------------------------------------------------------
    def test_field_order_validation(self) -> None:
        self.consumer.parse_line("VERSION\t1")
        # Swapping price and tick_number in TICK (TICK \t 1 \t 100.5 \t 42) -> tick_num must be integer
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t100.5\t42")

        # Swapping quantity and price in TRADE
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TRADE\t1\t10\t101\t102\t25\t99.5")

    # -------------------------------------------------------------------------
    # 3. Missing fields and extra fields
    # -------------------------------------------------------------------------
    def test_missing_and_extra_fields(self) -> None:
        self.consumer.parse_line("VERSION\t1")
        # VERSION with extra field
        c = IPCProtocolConsumer()
        with self.assertRaises(ProtocolError):
            c.parse_line("VERSION\t1\textra")

        # TICK missing price
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t42")
        # TICK extra field
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t42\t100.0\textra")

        # TRADE missing qty
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TRADE\t1\t10\t101\t102\t99.5")
        # TRADE extra field
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TRADE\t1\t10\t101\t102\t99.5\t25\textra")

        # PORTFOLIO missing total_pnl
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("PORTFOLIO\t1\t10000.0\t0\t0.0\t0.0\t0.0")
        # PORTFOLIO extra field
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("PORTFOLIO\t1\t10000.0\t0\t0.0\t0.0\t0.0\t0.0\textra")

        # END extra field
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("END\t1\textra")

    # -------------------------------------------------------------------------
    # 4. Invalid numeric values and non-finite floats
    # -------------------------------------------------------------------------
    def test_invalid_numeric_values_and_non_finite_floats(self) -> None:
        self.consumer.parse_line("VERSION\t1")

        # Non-digits for integers
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\tzero\t100.0")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TRADE\t1\t1\t1\t1\t100.0\tqty")

        # Zero tick number or zero trade qty
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t0\t100.0")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TRADE\t1\t1\t1\t1\t100.0\t0")

        # Non-positive prices
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t1\t0.0")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t1\t-10.0")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TRADE\t1\t1\t1\t1\t-50.0\t10")

        # Non-finite floats (NaN, Inf, -Inf)
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t1\tnan")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t1\tinf")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t1\t-inf")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("PORTFOLIO\t1\tnan\t0\t0.0\t0.0\t0.0\t0.0")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("PORTFOLIO\t1\t1000.0\t0\t0.0\tinf\t0.0\t0.0")

    # -------------------------------------------------------------------------
    # 5. Unknown message types and unsupported versions
    # -------------------------------------------------------------------------
    def test_unknown_message_types(self) -> None:
        self.consumer.parse_line("VERSION\t1")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("SNAPSHOT\t1\t100.0")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("FOOBAR\t1")

    def test_unsupported_protocol_version(self) -> None:
        c = IPCProtocolConsumer(expected_version=1)
        with self.assertRaises(ProtocolError):
            c.parse_line("VERSION\t2")
        with self.assertRaises(ProtocolError):
            c.parse_line("VERSION\t0")

    # -------------------------------------------------------------------------
    # 6. Missing or duplicate VERSION announcements
    # -------------------------------------------------------------------------
    def test_missing_version_announcement(self) -> None:
        c = IPCProtocolConsumer()
        # TICK sent first before VERSION
        with self.assertRaises(ProtocolError):
            c.parse_line("TICK\t1\t1\t100.0")

    def test_duplicate_version_announcement(self) -> None:
        c = IPCProtocolConsumer()
        c.parse_line("VERSION\t1")
        with self.assertRaises(ProtocolError):
            c.parse_line("VERSION\t1")

    # -------------------------------------------------------------------------
    # 7. Missing END and unexpected EOF
    # -------------------------------------------------------------------------
    def test_missing_end_unexpected_eof(self) -> None:
        c = IPCProtocolConsumer()
        stream = ["VERSION\t1\n", "TICK\t1\t1\t100.0\n"]
        with self.assertRaises(UnexpectedEOFError):
            list(c.consume_stream(stream))

    # -------------------------------------------------------------------------
    # 8. Messages after END
    # -------------------------------------------------------------------------
    def test_messages_after_end(self) -> None:
        self.consumer.parse_line("VERSION\t1")
        self.consumer.parse_line("END\t1")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("TICK\t1\t2\t101.0")
        with self.assertRaises(ProtocolError):
            self.consumer.parse_line("END\t1")

    # -------------------------------------------------------------------------
    # 9. Blank-line behavior
    # -------------------------------------------------------------------------
    def test_blank_line_behavior(self) -> None:
        # Default behavior: skip blank lines without error
        c_lenient = IPCProtocolConsumer(ignore_blank_lines=True)
        res1 = c_lenient.parse_line("\n")
        self.assertIsNone(res1)
        res2 = c_lenient.parse_line("   \r\n")
        self.assertIsNone(res2)

        # Strict mode: reject blank lines
        c_strict = IPCProtocolConsumer(ignore_blank_lines=False)
        with self.assertRaises(ProtocolError):
            c_strict.parse_line("\n")
        with self.assertRaises(ProtocolError):
            c_strict.parse_line("   \n")

    # -------------------------------------------------------------------------
    # 10. Successful end-to-end execution through Python launcher
    # -------------------------------------------------------------------------
    def test_launcher_end_to_end_success(self) -> None:
        producer_bin = project_root / "bin" / "ipc_producer"
        if not producer_bin.exists():
            producer_bin = project_root / "build" / "ipc_producer"

        self.assertTrue(producer_bin.exists(), f"Producer binary not found at {producer_bin}")

        launcher = IPCLauncher(
            executable_path=str(producer_bin),
            args=["--ticks", "5", "--seed", "123"],
        )

        received_types = []
        result = launcher.run(on_message=lambda m: received_types.append(type(m).__name__))

        self.assertEqual(result["status"], "success")
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["counts"]["VERSION"], 1)
        self.assertEqual(result["counts"]["TICK"], 5)
        self.assertEqual(result["counts"]["END"], 1)
        self.assertIn("VersionMessage", received_types)
        self.assertIn("TickMessage", received_types)
        self.assertIn("EndMessage", received_types)

    # -------------------------------------------------------------------------
    # 11. Non-zero producer exit handling
    # -------------------------------------------------------------------------
    def test_launcher_nonzero_producer_exit(self) -> None:
        producer_bin = project_root / "bin" / "ipc_producer"
        if not producer_bin.exists():
            producer_bin = project_root / "build" / "ipc_producer"

        # --fail-for-test triggers return code 42
        launcher = IPCLauncher(
            executable_path=str(producer_bin),
            args=["--fail-for-test"],
        )

        with self.assertRaises(ProducerError) as ctx:
            launcher.run()

        self.assertEqual(ctx.exception.exit_code, 42)
        self.assertIn("Intentional failure triggered", ctx.exception.stderr)

    def test_launcher_missing_executable(self) -> None:
        launcher = IPCLauncher(executable_path="/non/existent/path/to/binary")
        with self.assertRaises(FileNotFoundError):
            launcher.run()


if __name__ == "__main__":
    unittest.main()
