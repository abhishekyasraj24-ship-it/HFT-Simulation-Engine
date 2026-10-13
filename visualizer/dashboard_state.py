"""Dashboard state representation and event update logic.

Decouples protocol message handling, trade-side inference, and simulation
metrics from the Matplotlib rendering loop for deterministic unit testing.
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict, List, Optional, Tuple

from ipc_consumer import (
    EndMessage,
    IPCMessage,
    PortfolioMessage,
    TickMessage,
    TradeMessage,
    VersionMessage,
)


class EngineStatus(str, Enum):
    WAITING = "WAITING"
    RUNNING = "RUNNING"
    COMPLETED = "COMPLETED"
    ERROR = "ERROR"


class InferredSide(str, Enum):
    BUY = "BUY"
    SELL = "SELL"
    UNCLASSIFIED = "UNCLASSIFIED"


@dataclass
class ClassifiedTrade:
    """Represents an executed trade with its associated tick and inferred portfolio side."""
    trade_id: int
    tick_number: int
    price: float
    quantity: int
    side: InferredSide
    buy_order_id: int
    sell_order_id: int


@dataclass
class DashboardState:
    """Encapsulates the live state of the trading visualizer dashboard.

    Inference Policy for Trade Side:
      - The IPC TRADE schema reports execution details (trade ID, buy/sell order IDs,
        price, quantity), but does not explicitly tag which side belongs to the simulated portfolio.
      - When a TRADE arrives, it is associated with the most recent tick and recorded as pending.
      - When the subsequent PORTFOLIO update arrives:
          * If position_quantity increased, the trade is classified as BUY.
          * If position_quantity decreased, the trade is classified as SELL.
          * If position_quantity did not change or the delta cannot be matched,
            the trade is marked UNCLASSIFIED (external/unrelated trade).
      - If the stream ends before a corresponding portfolio change, pending trades
        remain UNCLASSIFIED. Sides are never invented.
    """

    status: EngineStatus = EngineStatus.WAITING
    error_message: Optional[str] = None
    protocol_version: Optional[int] = None
    ticks_received: int = 0
    trades_received: int = 0
    portfolio_updates_received: int = 0
    producer_completed_successfully: bool = False

    # Latest market tick observed
    latest_market_tick: int = 0

    # Price history
    tick_numbers: List[int] = field(default_factory=list)
    prices: List[float] = field(default_factory=list)

    # Trade history
    trades: List[ClassifiedTrade] = field(default_factory=list)
    _pending_trades: List[Tuple[TradeMessage, int, int]] = field(default_factory=list)
    _last_known_position: int = 0

    # Latest portfolio metrics (authoritative from PORTFOLIO messages)
    latest_portfolio: Optional[PortfolioMessage] = None

    # P&L history over time (associated with actual market tick numbers)
    pnl_ticks: List[int] = field(default_factory=list)
    total_pnl_history: List[float] = field(default_factory=list)
    realized_pnl_history: List[float] = field(default_factory=list)
    unrealized_pnl_history: List[float] = field(default_factory=list)

    def current_tick(self) -> int:
        """Returns the most recent market tick number, or 0 if none observed yet."""
        return self.latest_market_tick

    def process_message(self, msg: IPCMessage) -> None:
        """Updates dashboard state based on an incoming IPC protocol message."""
        if isinstance(msg, VersionMessage):
            self.protocol_version = msg.version
            if self.status == EngineStatus.WAITING:
                self.status = EngineStatus.RUNNING

        elif isinstance(msg, TickMessage):
            self.ticks_received += 1
            self.latest_market_tick = msg.tick_number
            self.tick_numbers.append(msg.tick_number)
            self.prices.append(msg.price)
            if self.status == EngineStatus.WAITING:
                self.status = EngineStatus.RUNNING

        elif isinstance(msg, TradeMessage):
            self.trades_received += 1
            cur_tick = self.latest_market_tick
            # Queue trade for portfolio correlation
            self._pending_trades.append((msg, cur_tick, self._last_known_position))

        elif isinstance(msg, PortfolioMessage):
            self.portfolio_updates_received += 1
            self.latest_portfolio = msg
            cur_tick = self.latest_market_tick

            # Correlate any pending trades with the position change
            delta_pos = msg.position_quantity - self._last_known_position
            if self._pending_trades:
                if len(self._pending_trades) == 1:
                    trade_msg, trade_tick, _ = self._pending_trades.pop(0)
                    if delta_pos > 0 and delta_pos == trade_msg.quantity:
                        side = InferredSide.BUY
                    elif delta_pos < 0 and abs(delta_pos) == trade_msg.quantity:
                        side = InferredSide.SELL
                    elif delta_pos > 0:
                        side = InferredSide.BUY
                    elif delta_pos < 0:
                        side = InferredSide.SELL
                    else:
                        side = InferredSide.UNCLASSIFIED

                    self.trades.append(
                        ClassifiedTrade(
                            trade_id=trade_msg.trade_id,
                            tick_number=trade_tick,
                            price=trade_msg.price,
                            quantity=trade_msg.quantity,
                            side=side,
                            buy_order_id=trade_msg.buy_order_id,
                            sell_order_id=trade_msg.sell_order_id,
                        )
                    )
                else:
                    # Multiple pending trades: check aggregate consistency
                    for trade_msg, trade_tick, _ in self._pending_trades:
                        if delta_pos > 0:
                            side = InferredSide.BUY
                        elif delta_pos < 0:
                            side = InferredSide.SELL
                        else:
                            side = InferredSide.UNCLASSIFIED

                        self.trades.append(
                            ClassifiedTrade(
                                trade_id=trade_msg.trade_id,
                                tick_number=trade_tick,
                                price=trade_msg.price,
                                quantity=trade_msg.quantity,
                                side=side,
                                buy_order_id=trade_msg.buy_order_id,
                                sell_order_id=trade_msg.sell_order_id,
                            )
                        )
                    self._pending_trades.clear()

            self._last_known_position = msg.position_quantity

            # Record P&L snapshot
            self.pnl_ticks.append(cur_tick)
            self.total_pnl_history.append(msg.total_pnl)
            self.realized_pnl_history.append(msg.realized_pnl)
            self.unrealized_pnl_history.append(msg.unrealized_pnl)

        elif isinstance(msg, EndMessage):
            # Finalize any remaining unclassified trades
            while self._pending_trades:
                trade_msg, trade_tick, _ = self._pending_trades.pop(0)
                self.trades.append(
                    ClassifiedTrade(
                        trade_id=trade_msg.trade_id,
                        tick_number=trade_tick,
                        price=trade_msg.price,
                        quantity=trade_msg.quantity,
                        side=InferredSide.UNCLASSIFIED,
                        buy_order_id=trade_msg.buy_order_id,
                        sell_order_id=trade_msg.sell_order_id,
                    )
                )
            self.status = EngineStatus.COMPLETED
            self.producer_completed_successfully = True

    def set_error(self, message: str) -> None:
        """Transitions state to ERROR and records diagnostic reason."""
        self.status = EngineStatus.ERROR
        self.error_message = message

    def get_buy_trades(self) -> List[ClassifiedTrade]:
        """Returns trades classified as portfolio buys."""
        return [t for t in self.trades if t.side == InferredSide.BUY]

    def get_sell_trades(self) -> List[ClassifiedTrade]:
        """Returns trades classified as portfolio sells."""
        return [t for t in self.trades if t.side == InferredSide.SELL]

    def get_unclassified_trades(self) -> List[ClassifiedTrade]:
        """Returns trades that could not be attributed to a portfolio side."""
        return [t for t in self.trades if t.side == InferredSide.UNCLASSIFIED]
