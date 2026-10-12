#include "Portfolio.hpp"

#include <cmath>
#include <stdexcept>

Portfolio::Portfolio(double starting_cash) {
    reset(starting_cash);
}

void Portfolio::reset(double starting_cash) {
    if (!std::isfinite(starting_cash) || starting_cash < 0.0) {
        throw std::invalid_argument("Starting cash must be a non-negative finite number");
    }
    starting_cash_ = starting_cash;
    cash_ = starting_cash;
    position_quantity_ = 0;
    average_entry_price_ = 0.0;
    realized_pnl_ = 0.0;
    processed_trade_ids_.clear();
    last_error_.clear();
}

bool Portfolio::has_processed_trade(TradeId trade_id) const {
    if (trade_id == 0) {
        return false;
    }
    return processed_trade_ids_.find(trade_id) != processed_trade_ids_.end();
}

bool Portfolio::can_process_trade(const Trade& trade, OrderSide side, std::string* error_out) const {
    // 1. Quantity validation
    if (trade.quantity == 0) {
        if (error_out) *error_out = "Trade quantity must be greater than zero";
        return false;
    }

    // 2. Price validation
    if (!std::isfinite(trade.price) || trade.price <= 0.0) {
        if (error_out) *error_out = "Trade price must be a strictly positive finite number";
        return false;
    }

    // 3. Trade deduplication
    if (trade.trade_id != 0 && has_processed_trade(trade.trade_id)) {
        if (error_out) *error_out = "Duplicate trade ID: trade #" + std::to_string(trade.trade_id) + " has already been processed";
        return false;
    }

    // 4. Side-specific checks
    if (side == OrderSide::Buy) {
        double total_cost = static_cast<double>(trade.quantity) * trade.price;
        if (total_cost > cash_) {
            if (error_out) *error_out = "Insufficient cash: trade cost $" + std::to_string(total_cost) +
                                       " exceeds available cash $" + std::to_string(cash_);
            return false;
        }
    } else if (side == OrderSide::Sell) {
        if (trade.quantity > position_quantity_) {
            if (error_out) *error_out = "Cannot sell " + std::to_string(trade.quantity) +
                                       " units: exceeds current long position of " + std::to_string(position_quantity_) + " units";
            return false;
        }
    }

    return true;
}

bool Portfolio::process_trade(const Trade& trade, OrderSide side) {
    std::string err;
    if (!can_process_trade(trade, side, &err)) {
        last_error_ = err;
        return false;
    }
    last_error_.clear();

    double trade_value = static_cast<double>(trade.quantity) * trade.price;

    if (side == OrderSide::Buy) {
        // Deduct cash
        cash_ -= trade_value;

        // Calculate new volume-weighted average entry price
        double old_total_cost = static_cast<double>(position_quantity_) * average_entry_price_;
        double new_total_cost = old_total_cost + trade_value;
        position_quantity_ += trade.quantity;
        average_entry_price_ = new_total_cost / static_cast<double>(position_quantity_);
    } else { // OrderSide::Sell
        // Add proceeds to cash
        cash_ += trade_value;

        // Realized P&L = units_sold * (sale_price - average_entry_price)
        double pnl_on_trade = static_cast<double>(trade.quantity) * (trade.price - average_entry_price_);
        realized_pnl_ += pnl_on_trade;

        position_quantity_ -= trade.quantity;

        // Reset average entry price when position completely closes
        if (position_quantity_ == 0) {
            average_entry_price_ = 0.0;
        }
    }

    if (trade.trade_id != 0) {
        processed_trade_ids_.insert(trade.trade_id);
    }

    return true;
}

void Portfolio::apply_trade(const Trade& trade, OrderSide side) {
    if (!process_trade(trade, side)) {
        throw std::runtime_error("Portfolio trade failed: " + last_error_);
    }
}

double Portfolio::get_unrealized_pnl(double current_market_price) const {
    if (!std::isfinite(current_market_price) || current_market_price < 0.0) {
        throw std::invalid_argument("Market price must be a non-negative finite number");
    }
    if (position_quantity_ == 0) {
        return 0.0;
    }
    return static_cast<double>(position_quantity_) * (current_market_price - average_entry_price_);
}

double Portfolio::get_total_pnl(double current_market_price) const {
    return realized_pnl_ + get_unrealized_pnl(current_market_price);
}

double Portfolio::get_total_equity(double current_market_price) const {
    if (!std::isfinite(current_market_price) || current_market_price < 0.0) {
        throw std::invalid_argument("Market price must be a non-negative finite number");
    }
    return cash_ + (static_cast<double>(position_quantity_) * current_market_price);
}
