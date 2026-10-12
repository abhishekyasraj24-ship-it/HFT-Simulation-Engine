#include "ExecutionEngine.hpp"

#include <algorithm>

ExecutionResult ExecutionEngine::submit_order(Order order) {
    ExecutionResult result;
    result.order = order;

    // 1. Validation: Reject invalid orders using existing validation logic
    if (!result.order.is_valid()) {
        result.order.status = OrderStatus::Rejected;
        return result;
    }

    // 2. Match against existing resting liquidity
    if (result.order.side == OrderSide::Buy) {
        match_buy(result);
    } else {
        match_sell(result);
    }

    // 3. Post-matching handling of unfilled remainder
    if (result.order.remaining_quantity() == 0) {
        result.order.status = OrderStatus::Filled;
    } else {
        if (result.order.type == OrderType::Market) {
            // Market orders cancel any unfilled remainder immediately
            result.order.status = OrderStatus::Cancelled;
        } else if (result.order.type == OrderType::Limit) {
            // Limit orders rest in the order book at their limit price
            if (result.order.filled_quantity > 0) {
                result.order.status = OrderStatus::PartiallyFilled;
            } else {
                result.order.status = OrderStatus::New;
            }
            book_.add_limit_order(result.order);
        }
    }

    return result;
}

void ExecutionEngine::match_buy(ExecutionResult& result) {
    while (result.order.remaining_quantity() > 0 && book_.has_asks()) {
        Order& best_ask = book_.get_best_ask_order();
        double match_price = best_ask.limit_price.value();

        // Limit-price protection: Buy limit order cannot execute above its limit price
        if (result.order.type == OrderType::Limit) {
            if (result.order.limit_price.value() < match_price) {
                break;
            }
        }

        // Match quantity bounded by both incoming and resting order remaining quantities
        std::uint32_t match_qty = std::min(result.order.remaining_quantity(),
                                           best_ask.remaining_quantity());

        Trade trade{
            next_trade_id_++,
            result.order.id, // buy_order_id
            best_ask.id,     // sell_order_id
            match_price,
            match_qty
        };

        result.order.filled_quantity += match_qty;
        best_ask.filled_quantity += match_qty;

        if (best_ask.remaining_quantity() == 0) {
            best_ask.status = OrderStatus::Filled;
            book_.pop_best_ask_order();
        } else {
            best_ask.status = OrderStatus::PartiallyFilled;
        }

        result.trades.push_back(trade);
        trades_.push_back(trade);
    }
}

void ExecutionEngine::match_sell(ExecutionResult& result) {
    while (result.order.remaining_quantity() > 0 && book_.has_bids()) {
        Order& best_bid = book_.get_best_bid_order();
        double match_price = best_bid.limit_price.value();

        // Limit-price protection: Sell limit order cannot execute below its limit price
        if (result.order.type == OrderType::Limit) {
            if (result.order.limit_price.value() > match_price) {
                break;
            }
        }

        // Match quantity bounded by both incoming and resting order remaining quantities
        std::uint32_t match_qty = std::min(result.order.remaining_quantity(),
                                           best_bid.remaining_quantity());

        Trade trade{
            next_trade_id_++,
            best_bid.id,     // buy_order_id
            result.order.id, // sell_order_id
            match_price,
            match_qty
        };

        result.order.filled_quantity += match_qty;
        best_bid.filled_quantity += match_qty;

        if (best_bid.remaining_quantity() == 0) {
            best_bid.status = OrderStatus::Filled;
            book_.pop_best_bid_order();
        } else {
            best_bid.status = OrderStatus::PartiallyFilled;
        }

        result.trades.push_back(trade);
        trades_.push_back(trade);
    }
}

void ExecutionEngine::reset() {
    book_.clear();
    trades_.clear();
    next_trade_id_ = 1;
}
