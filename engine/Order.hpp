#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

/**
 * @brief Unique identifier for an order.
 */
using OrderId = std::uint64_t;

/**
 * @brief Direction/side of the order.
 */
enum class OrderSide {
    Buy,
    Sell
};

/**
 * @brief Execution type of the order.
 */
enum class OrderType {
    Market,
    Limit
};

/**
 * @brief Lifecycle status of the order.
 *
 * NOTE: This represents order state in the simulation pipeline.
 * An order with status New is simply created; it has not been routed or executed.
 */
enum class OrderStatus {
    New,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

/**
 * @brief Converts an OrderSide to a readable string.
 */
inline std::string to_string(OrderSide side) {
    switch (side) {
        case OrderSide::Buy:
            return "BUY";
        case OrderSide::Sell:
            return "SELL";
    }
    return "UNKNOWN";
}

/**
 * @brief Converts an OrderType to a readable string.
 */
inline std::string to_string(OrderType type) {
    switch (type) {
        case OrderType::Market:
            return "MARKET";
        case OrderType::Limit:
            return "LIMIT";
    }
    return "UNKNOWN";
}

/**
 * @brief Converts an OrderStatus to a readable string.
 */
inline std::string to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::New:
            return "NEW";
        case OrderStatus::PartiallyFilled:
            return "PARTIALLY_FILLED";
        case OrderStatus::Filled:
            return "FILLED";
        case OrderStatus::Cancelled:
            return "CANCELLED";
        case OrderStatus::Rejected:
            return "REJECTED";
    }
    return "UNKNOWN";
}

/**
 * @brief Represents a single trading order in the simulation system.
 */
struct Order {
    OrderId id{0};
    OrderSide side{OrderSide::Buy};
    OrderType type{OrderType::Market};
    std::uint32_t quantity{0};
    std::optional<double> limit_price{std::nullopt};
    OrderStatus status{OrderStatus::New};
    std::uint32_t filled_quantity{0};

    /**
     * @brief Computes remaining unfilled quantity for this order.
     */
    std::uint32_t remaining_quantity() const {
        return (quantity > filled_quantity) ? (quantity - filled_quantity) : 0;
    }

    /**
     * @brief Checks if the order specification is valid according to trading rules.
     * @return std::optional<std::string> Returns std::nullopt if valid, or a descriptive
     *         error message if validation fails.
     */
    std::optional<std::string> get_validation_error() const {
        if (quantity == 0) {
            return "Order quantity must be strictly greater than zero";
        }

        if (type == OrderType::Limit) {
            if (!limit_price.has_value()) {
                return "Limit orders must specify a limit price";
            }
            if (limit_price.value() <= 0.0) {
                return "Limit price must be strictly positive";
            }
        } else if (type == OrderType::Market) {
            if (limit_price.has_value()) {
                return "Market orders must not specify a limit price";
            }
        }

        return std::nullopt;
    }

    /**
     * @brief Returns true if the order satisfies all validation constraints.
     */
    bool is_valid() const {
        return !get_validation_error().has_value();
    }

    /**
     * @brief Validates the order, throwing std::invalid_argument if invalid.
     */
    void validate() const {
        auto error = get_validation_error();
        if (error.has_value()) {
            throw std::invalid_argument("Order validation failed: " + error.value());
        }
    }

    /**
     * @brief Helper to generate a human-readable summary string for the order.
     */
    std::string to_string_summary() const {
        std::ostringstream oss;
        oss << "Order #" << id << " [" << to_string(status) << "] "
            << to_string(side) << " " << quantity << " " << to_string(type);
        if (limit_price.has_value()) {
            oss << " @ $" << limit_price.value();
        }
        if (filled_quantity > 0) {
            oss << " (Filled: " << filled_quantity << "/" << quantity << ")";
        }
        return oss.str();
    }

    // Convenient static factory helpers
    static Order create_market(OrderId id, OrderSide side, std::uint32_t quantity) {
        Order o{id, side, OrderType::Market, quantity, std::nullopt, OrderStatus::New, 0};
        o.validate();
        return o;
    }

    static Order create_limit(OrderId id, OrderSide side, std::uint32_t quantity, double limit_price) {
        Order o{id, side, OrderType::Limit, quantity, limit_price, OrderStatus::New, 0};
        o.validate();
        return o;
    }
};
