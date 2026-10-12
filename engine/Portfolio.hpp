#pragma once

#include "Order.hpp"
#include "Trade.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>

/**
 * @brief Manages cash, asset positions, and P&L for a single simulated asset.
 *
 * Accounting Conventions:
 * - Long-Only: Short selling is not permitted. Sells exceeding current position are rejected.
 * - Average-Cost Basis: Additional purchases calculate a volume-weighted average price.
 * - Realized P&L is realized upon selling against the average entry price.
 * - When the position reaches zero, the average entry price is reset to 0.0.
 * - Unrealized P&L is calculated using a provided mark-to-market price.
 * - Total P&L = Realized P&L + Unrealized P&L (Total Equity = Starting Cash + Total P&L).
 * - Fees, taxes, and slippage are currently ignored (0.0).
 * - Trade deduplication: Each unique non-zero TradeId is processed at most once.
 */
class Portfolio {
public:
    /**
     * @brief Constructs a portfolio with initial cash.
     * @param starting_cash Starting available cash (must be non-negative and finite).
     * @throws std::invalid_argument if starting_cash is negative or non-finite.
     */
    explicit Portfolio(double starting_cash = 10000.0);

    /**
     * @brief Processes an executed trade, updating position, cash, and realized P&L.
     * @param trade The executed trade record.
     * @param side The side of the trade for this portfolio (Buy or Sell).
     * @return true if the trade was valid and successfully applied; false if rejected.
     */
    bool process_trade(const Trade& trade, OrderSide side);

    /**
     * @brief Processes an executed trade, throwing an exception on failure.
     * @throws std::runtime_error if trade cannot be processed.
     */
    void apply_trade(const Trade& trade, OrderSide side);

    /**
     * @brief Validates if a trade can be processed without altering portfolio state.
     * @param trade The trade to check.
     * @param side Buy or Sell side.
     * @param error_out Optional pointer to receive a descriptive error message on failure.
     * @return true if trade is acceptable.
     */
    bool can_process_trade(const Trade& trade, OrderSide side, std::string* error_out = nullptr) const;

    // Getters for portfolio metrics
    double get_starting_cash() const { return starting_cash_; }
    double get_cash() const { return cash_; }
    std::uint32_t get_position_quantity() const { return position_quantity_; }
    double get_average_entry_price() const { return average_entry_price_; }
    double get_realized_pnl() const { return realized_pnl_; }

    /**
     * @brief Calculates unrealized P&L using the provided market price.
     * @param current_market_price Current mark-to-market price.
     * @return double Unrealized P&L (position_qty * (market_price - avg_entry_price)).
     */
    double get_unrealized_pnl(double current_market_price) const;

    /**
     * @brief Calculates total P&L (Realized P&L + Unrealized P&L).
     */
    double get_total_pnl(double current_market_price) const;

    /**
     * @brief Calculates total portfolio equity (Cash + Position Value).
     */
    double get_total_equity(double current_market_price) const;

    /**
     * @brief Returns the reason for the most recent trade processing failure.
     */
    const std::string& get_last_error() const { return last_error_; }

    /**
     * @brief Resets the portfolio back to starting conditions.
     */
    void reset(double starting_cash = 10000.0);

    /**
     * @brief Returns whether a specific trade ID has already been processed.
     */
    bool has_processed_trade(TradeId trade_id) const;

private:
    double starting_cash_{10000.0};
    double cash_{10000.0};
    std::uint32_t position_quantity_{0};
    double average_entry_price_{0.0};
    double realized_pnl_{0.0};

    std::unordered_set<TradeId> processed_trade_ids_;
    std::string last_error_;
};
