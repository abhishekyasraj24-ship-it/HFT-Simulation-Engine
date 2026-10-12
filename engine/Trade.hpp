#pragma once

#include "Order.hpp"
#include <cstdint>
#include <sstream>
#include <string>

/**
 * @brief Unique identifier for a trade execution.
 */
using TradeId = std::uint64_t;

/**
 * @brief Represents an executed trade between a buyer and a seller.
 */
struct Trade {
    TradeId trade_id{0};
    OrderId buy_order_id{0};
    OrderId sell_order_id{0};
    double price{0.0};
    std::uint32_t quantity{0};

    std::string to_string_summary() const {
        std::ostringstream oss;
        oss << "Trade #" << trade_id << ": " << quantity << " units @ $"
            << price << " [Buyer #" << buy_order_id << ", Seller #" << sell_order_id << "]";
        return oss.str();
    }
};
