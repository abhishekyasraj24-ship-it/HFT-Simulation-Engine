#pragma once

#include "Order.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <vector>

/**
 * @brief Manages resting limit orders applying Price-Time Priority.
 *
 * - Buy Book (Bids): Highest price has priority.
 * - Sell Book (Asks): Lowest price has priority.
 * - Time Priority: Orders arriving earlier at the same price level execute first (FIFO).
 */
class OrderBook {
public:
    OrderBook() = default;

    /**
     * @brief Adds a resting limit order into the book.
     * @param order Limit order to rest in the book.
     */
    void add_limit_order(const Order& order);

    /**
     * @brief Checks if there are active buy orders.
     */
    bool has_bids() const;

    /**
     * @brief Checks if there are active sell orders.
     */
    bool has_asks() const;

    /**
     * @brief Checks if the entire book is empty.
     */
    bool empty() const;

    /**
     * @brief Returns the best (highest) bid price if bids exist.
     */
    std::optional<double> get_best_bid_price() const;

    /**
     * @brief Returns the best (lowest) ask price if asks exist.
     */
    std::optional<double> get_best_ask_price() const;

    /**
     * @brief Access the highest priority resting buy order.
     */
    Order& get_best_bid_order();
    const Order& get_best_bid_order() const;

    /**
     * @brief Access the highest priority resting sell order.
     */
    Order& get_best_ask_order();
    const Order& get_best_ask_order() const;

    /**
     * @brief Removes the highest priority buy order when completely filled.
     */
    void pop_best_bid_order();

    /**
     * @brief Removes the highest priority sell order when completely filled.
     */
    void pop_best_ask_order();

    /**
     * @brief Returns total number of active bid orders.
     */
    std::size_t total_bid_orders() const;

    /**
     * @brief Returns total number of active ask orders.
     */
    std::size_t total_ask_orders() const;

    /**
     * @brief Returns total aggregate remaining quantity across all bids.
     */
    std::uint32_t total_bid_volume() const;

    /**
     * @brief Returns total aggregate remaining quantity across all asks.
     */
    std::uint32_t total_ask_volume() const;

    /**
     * @brief Returns all resting bids in price-time priority sequence.
     */
    std::vector<Order> get_resting_bids() const;

    /**
     * @brief Returns all resting asks in price-time priority sequence.
     */
    std::vector<Order> get_resting_asks() const;

    /**
     * @brief Clears all resting orders from the book.
     */
    void clear();

private:
    // Bids: Sorted descending by price (highest price first).
    // Deque maintains FIFO arrival order at each specific price level.
    std::map<double, std::deque<Order>, std::greater<double>> bids_;

    // Asks: Sorted ascending by price (lowest price first).
    // Deque maintains FIFO arrival order at each specific price level.
    std::map<double, std::deque<Order>, std::less<double>> asks_;
};
