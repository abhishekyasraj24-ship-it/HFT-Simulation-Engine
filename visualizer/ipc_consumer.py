"""C++ <-> Python IPC Protocol Consumer and Subprocess Launcher.

This module provides a pure standard-library implementation of the versioned
newline-delimited, tab-separated message protocol used by the HFT Simulation Engine.
It includes:
  - Structured dataclass message definitions
  - Stream-based validation and parsing with strict error detection
  - Subprocess launcher utilizing OS pipes with safe, deadlock-free stderr handling
"""

from dataclasses import dataclass
from enum import Enum
import math
import os
from pathlib import Path
import subprocess
import sys
import threading
from typing import Any, Callable, Dict, Iterable, Iterator, List, Optional, Union


# =============================================================================
# Protocol Constants and Exception Hierarchy
# =============================================================================

DEFAULT_PROTOCOL_VERSION: int = 1


class IPCError(Exception):
    """Base exception for all IPC and protocol errors."""
    pass


class ProtocolError(IPCError):
    """Raised when a message violates the protocol specification."""
    pass


class UnexpectedEOFError(ProtocolError):
    """Raised when the protocol stream terminates before receiving an END message."""
    pass


class ProducerError(IPCError):
    """Raised when the C++ producer process terminates with a non-zero exit code."""
    def __init__(self, exit_code: int, stderr: str, message: Optional[str] = None):
        self.exit_code = exit_code
        self.stderr = stderr
        msg = message or f"Producer process exited with non-zero status code {exit_code}: {stderr}"
        super().__init__(msg)


class MessageType(Enum):
    VERSION = "VERSION"
    TICK = "TICK"
    TRADE = "TRADE"
    PORTFOLIO = "PORTFOLIO"
    END = "END"


# =============================================================================
# Structured Message Representations
# =============================================================================

@dataclass(frozen=True)
class VersionMessage:
    version: int


@dataclass(frozen=True)
class TickMessage:
    version: int
    tick_number: int
    price: float


@dataclass(frozen=True)
class TradeMessage:
    version: int
    trade_id: int
    buy_order_id: int
    sell_order_id: int
    price: float
    quantity: int


@dataclass(frozen=True)
class PortfolioMessage:
    version: int
    cash: float
    position_quantity: int
    average_entry_price: float
    realized_pnl: float
    unrealized_pnl: float
    total_pnl: float


@dataclass(frozen=True)
class EndMessage:
    version: int


IPCMessage = Union[VersionMessage, TickMessage, TradeMessage, PortfolioMessage, EndMessage]


# =============================================================================
# Field Parsing and Validation Helpers
# =============================================================================

def _parse_uint(token: str, field_name: str, allow_zero: bool = False) -> int:
    """Parses an unsigned integer and verifies domain constraints."""
    if not token:
        raise ProtocolError(f"Field '{field_name}' cannot be empty")
    # Disallow signs or whitespace
    if not token.isdigit():
        raise ProtocolError(f"Field '{field_name}' must be an unsigned integer, got '{token}'")
    try:
        val = int(token)
    except ValueError as ex:
        raise ProtocolError(f"Field '{field_name}' failed integer conversion: {ex}") from ex

    if not allow_zero and val == 0:
        raise ProtocolError(f"Field '{field_name}' must be strictly positive (> 0), got 0")
    if val < 0:
        raise ProtocolError(f"Field '{field_name}' cannot be negative, got {val}")
    return val


def _parse_finite_float(token: str, field_name: str, strictly_positive: bool = False, non_negative: bool = False) -> float:
    """Parses a float and strictly checks for finiteness (rejecting NaN, Inf)."""
    if not token:
        raise ProtocolError(f"Field '{field_name}' cannot be empty")
    token_lower = token.lower()
    if any(kw in token_lower for kw in ("nan", "inf")):
        raise ProtocolError(f"Field '{field_name}' must be a finite number; got '{token}'")
    try:
        val = float(token)
    except ValueError as ex:
        raise ProtocolError(f"Field '{field_name}' failed float conversion: '{token}'") from ex

    if not math.isfinite(val):
        raise ProtocolError(f"Field '{field_name}' must be finite; got {val}")
    if strictly_positive and val <= 0.0:
        raise ProtocolError(f"Field '{field_name}' must be strictly positive (> 0.0), got {val}")
    if non_negative and val < 0.0:
        raise ProtocolError(f"Field '{field_name}' cannot be negative, got {val}")
    return val


# =============================================================================
# Protocol Consumer
# =============================================================================

class IPCProtocolConsumer:
    """Parses and validates incoming lines from a C++ engine protocol stream.

    Validation Rules:
      1. Every message must be newline-delimited and tab-separated.
      2. The very first message must be 'VERSION'.
      3. Protocol version must match the supported version (default: 1).
      4. Duplicate or misplaced 'VERSION' messages are rejected.
      5. Exactly expected field counts are enforced per message type.
      6. All numeric types must be valid, within bounds, and floats must be finite.
      7. No messages may appear after 'END'.
      8. Blank lines are skipped by default (or rejected if ignore_blank_lines=False).
    """

    def __init__(self, expected_version: int = DEFAULT_PROTOCOL_VERSION, ignore_blank_lines: bool = True):
        self.expected_version = expected_version
        self.ignore_blank_lines = ignore_blank_lines

        # State tracking
        self.version_received: bool = False
        self.end_received: bool = False
        self.active_version: Optional[int] = None

        # Statistics
        self.message_counts: Dict[str, int] = {
            "VERSION": 0,
            "TICK": 0,
            "TRADE": 0,
            "PORTFOLIO": 0,
            "END": 0,
        }

    def reset(self) -> None:
        """Resets the consumer state for a new stream."""
        self.version_received = False
        self.end_received = False
        self.active_version = None
        for k in self.message_counts:
            self.message_counts[k] = 0

    def parse_line(self, raw_line: str) -> Optional[IPCMessage]:
        """Parses a single line from the protocol stream.

        Returns:
            IPCMessage if a valid message was parsed.
            None if the line was blank and ignore_blank_lines is True.

        Raises:
            ProtocolError if the line violates protocol rules.
        """
        line = raw_line.rstrip("\r\n")

        # Handle blank line policy
        if not line or line.isspace():
            if self.ignore_blank_lines:
                return None
            raise ProtocolError("Blank line encountered in strict protocol mode")

        tokens = line.split("\t")
        if not tokens:
            return None

        msg_type_str = tokens[0]

        # 1. State check: Messages after END
        if self.end_received:
            raise ProtocolError(f"Protocol violation: received message '{msg_type_str}' after 'END'")

        # 2. State check: First message MUST be VERSION
        if not self.version_received:
            if msg_type_str != "VERSION":
                raise ProtocolError(
                    f"Protocol violation: expected 'VERSION' as first message, but got '{msg_type_str}'"
                )

        # 3. State check: Duplicate VERSION
        if msg_type_str == "VERSION" and self.version_received:
            raise ProtocolError("Protocol violation: duplicate or misplaced 'VERSION' announcement received")

        # 4. Check version field (every message has version at token[1])
        if len(tokens) < 2:
            raise ProtocolError(
                f"Protocol violation: message '{msg_type_str}' is missing version field (expected >= 2 fields, got {len(tokens)})"
            )

        ver = _parse_uint(tokens[1], "version", allow_zero=False)
        if ver != self.expected_version:
            raise ProtocolError(
                f"Unsupported protocol version: {ver} (expected {self.expected_version})"
            )

        # 5. Type-specific parsing and validation
        if msg_type_str == "VERSION":
            if len(tokens) != 2:
                raise ProtocolError(f"VERSION message must have exactly 2 fields, got {len(tokens)}")
            self.version_received = True
            self.active_version = ver
            self.message_counts["VERSION"] += 1
            return VersionMessage(version=ver)

        elif msg_type_str == "TICK":
            if len(tokens) != 4:
                raise ProtocolError(f"TICK message must have exactly 4 fields, got {len(tokens)}")
            tick_num = _parse_uint(tokens[2], "tick_number", allow_zero=False)
            price = _parse_finite_float(tokens[3], "market_price", strictly_positive=True)
            self.message_counts["TICK"] += 1
            return TickMessage(version=ver, tick_number=tick_num, price=price)

        elif msg_type_str == "TRADE":
            if len(tokens) != 7:
                raise ProtocolError(f"TRADE message must have exactly 7 fields, got {len(tokens)}")
            trade_id = _parse_uint(tokens[2], "trade_id", allow_zero=False)
            buy_order_id = _parse_uint(tokens[3], "buy_order_id", allow_zero=False)
            sell_order_id = _parse_uint(tokens[4], "sell_order_id", allow_zero=False)
            price = _parse_finite_float(tokens[5], "price", strictly_positive=True)
            quantity = _parse_uint(tokens[6], "quantity", allow_zero=False)
            self.message_counts["TRADE"] += 1
            return TradeMessage(
                version=ver,
                trade_id=trade_id,
                buy_order_id=buy_order_id,
                sell_order_id=sell_order_id,
                price=price,
                quantity=quantity,
            )

        elif msg_type_str == "PORTFOLIO":
            if len(tokens) != 8:
                raise ProtocolError(f"PORTFOLIO message must have exactly 8 fields, got {len(tokens)}")
            cash = _parse_finite_float(tokens[2], "cash")
            position_qty = _parse_uint(tokens[3], "position_quantity", allow_zero=True)
            avg_entry_price = _parse_finite_float(tokens[4], "average_entry_price", non_negative=True)
            realized_pnl = _parse_finite_float(tokens[5], "realized_pnl")
            unrealized_pnl = _parse_finite_float(tokens[6], "unrealized_pnl")
            total_pnl = _parse_finite_float(tokens[7], "total_pnl")
            self.message_counts["PORTFOLIO"] += 1
            return PortfolioMessage(
                version=ver,
                cash=cash,
                position_quantity=position_qty,
                average_entry_price=avg_entry_price,
                realized_pnl=realized_pnl,
                unrealized_pnl=unrealized_pnl,
                total_pnl=total_pnl,
            )

        elif msg_type_str == "END":
            if len(tokens) != 2:
                raise ProtocolError(f"END message must have exactly 2 fields, got {len(tokens)}")
            self.end_received = True
            self.message_counts["END"] += 1
            return EndMessage(version=ver)

        else:
            raise ProtocolError(f"Unknown message type: '{msg_type_str}'")

    def consume_stream(self, stream: Iterable[str]) -> Iterator[IPCMessage]:
        """Consumes lines from an iterable (e.g., file-like object or pipe),
        yielding parsed messages and verifying that the stream terminates with END.
        """
        for line in stream:
            msg = self.parse_line(line)
            if msg is not None:
                yield msg

        # When stream finishes, verify clean completion
        if not self.end_received:
            raise UnexpectedEOFError("Stream terminated before receiving 'END' message")


# =============================================================================
# IPC Launcher
# =============================================================================

class IPCLauncher:
    """Spawns the C++ engine producer via subprocess.Popen with standard OS pipes,
    reads stdout incrementally, drains stderr on a background thread to prevent deadlocks,
    and cleans up child resources safely.
    """

    def __init__(
        self,
        executable_path: Optional[str] = None,
        args: Optional[List[str]] = None,
        consumer: Optional[IPCProtocolConsumer] = None,
    ):
        self.executable_path = executable_path or self._discover_default_executable()
        self.args = args or []
        self.consumer = consumer or IPCProtocolConsumer()
        self.process: Optional[subprocess.Popen] = None

    @staticmethod
    def _discover_default_executable() -> str:
        """Finds the default C++ producer binary path."""
        candidates = [
            Path("./bin/ipc_producer"),
            Path("./build/ipc_producer"),
            Path("../bin/ipc_producer"),
            Path("../build/ipc_producer"),
        ]
        for c in candidates:
            if c.is_file() and os.access(c, os.X_OK):
                return str(c.resolve())
        # Fall back to ./bin/ipc_producer
        return str(Path("./bin/ipc_producer").resolve())

    def run(
        self,
        on_message: Optional[Callable[[IPCMessage], None]] = None,
    ) -> Dict[str, Any]:
        """Runs the producer binary, consumes messages incrementally, and returns summary stats.

        Args:
            on_message: Optional callback invoked for each received IPCMessage.

        Returns:
            Dictionary containing consumption statistics, messages, and producer status.
        """
        exec_path = str(self.executable_path)
        if not os.path.isfile(exec_path):
            raise FileNotFoundError(f"Producer executable not found at '{exec_path}'")
        if not os.access(exec_path, os.X_OK):
            raise PermissionError(f"Producer file '{exec_path}' is not executable")

        cmd = [exec_path] + self.args
        self.consumer.reset()

        stderr_chunks: List[str] = []
        messages: List[IPCMessage] = []
        process: Optional[subprocess.Popen] = None
        stderr_thread: Optional[threading.Thread] = None

        def drain_stderr(proc: subprocess.Popen) -> None:
            """Reads stderr incrementally into a buffer to prevent pipe deadlock."""
            try:
                if proc.stderr:
                    for line in iter(proc.stderr.readline, ""):
                        stderr_chunks.append(line)
            except (ValueError, OSError):
                pass

        try:
            # Start child process using OS pipes; avoid shell=True
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,  # Line-buffered
            )
            self.process = process

            # Background thread prevents pipe deadlock if child fills stderr buffer
            stderr_thread = threading.Thread(
                target=drain_stderr, args=(process,), daemon=True
            )
            stderr_thread.start()

            # Incrementally consume stdout stream
            if process.stdout is not None:
                for line in iter(process.stdout.readline, ""):
                    msg = self.consumer.parse_line(line)
                    if msg is not None:
                        messages.append(msg)
                        if on_message:
                            on_message(msg)

            # Wait for child process to finish
            process.wait()

            # Ensure stderr thread completes
            if stderr_thread and stderr_thread.is_alive():
                stderr_thread.join(timeout=1.0)

            # Check if producer finished with non-zero exit code
            if process.returncode != 0:
                err_text = "".join(stderr_chunks).strip()
                raise ProducerError(
                    exit_code=process.returncode,
                    stderr=err_text,
                    message=f"Producer failed with exit code {process.returncode}: {err_text}",
                )

            # If exit was 0 but END was never received, report unexpected EOF
            if not self.consumer.end_received:
                raise UnexpectedEOFError(
                    "Producer exited successfully (code 0) but did not send 'END' message"
                )

        except (KeyboardInterrupt, Exception):
            # Safe process cleanup on interrupt or exception
            if process and process.poll() is None:
                try:
                    process.terminate()
                    process.wait(timeout=1.0)
                except Exception:
                    try:
                        process.kill()
                        process.wait(timeout=1.0)
                    except Exception:
                        pass
            raise

        finally:
            if stderr_thread and stderr_thread.is_alive():
                try:
                    stderr_thread.join(timeout=1.0)
                except Exception:
                    pass
            # Guarantee stream closure
            if process:
                if process.stdout and not process.stdout.closed:
                    try:
                        process.stdout.close()
                    except Exception:
                        pass
                if process.stderr and not process.stderr.closed:
                    try:
                        process.stderr.close()
                    except Exception:
                        pass

        stderr_str = "".join(stderr_chunks).strip()

        return {
            "status": "success",
            "exit_code": process.returncode if process else 0,
            "stderr": stderr_str,
            "counts": dict(self.consumer.message_counts),
            "messages": messages,
        }

    def terminate(self) -> None:
        """Terminates the child process if it is currently running."""
        proc = self.process
        if proc and proc.poll() is None:
            try:
                proc.terminate()
                proc.wait(timeout=1.0)
            except Exception:
                try:
                    proc.kill()
                    proc.wait(timeout=1.0)
                except Exception:
                    pass
