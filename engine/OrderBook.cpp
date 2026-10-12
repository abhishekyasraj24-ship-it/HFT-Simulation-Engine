#include "OrderBook.hpp"

#include <stdexcept>

void OrderBook::add_limit_order(const Order& order) {
    if (order.type != OrderType::Limit || !order.limit_price.has_value()) {
        throw std::invalid_argument("Only valid limit orders can be added to the order book");
    }

    if (order.remaining_quantity() == 0) {
        return;
    }

    double price = order.limit_price.value();
    if (order.side == OrderSide::Buy) {
        bids_[price].push_back(order);
    } else {
        asks_[price].push_back(order);
    }
}

bool OrderBook::has_bids() const {
    return !bids_.empty();
}

bool OrderBook::has_asks() const {
    return !asks_.empty();
}

bool OrderBook::empty() const {
    return bids_.empty() && asks_.empty();
}

std::optional<double> OrderBook::get_best_bid_price() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<double> OrderBook::get_best_ask_price() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

Order& OrderBook::get_best_bid_order() {
    if (bids_.empty() || bids_.begin()->second.empty()) {
        throw std::runtime_error("Attempted to access best bid on empty bid book");
    }
    return bids_.begin()->second.front();
}

const Order& OrderBook::get_best_bid_order() const {
    if (bids_.empty() || bids_.begin()->second.empty()) {
        throw std::runtime_error("Attempted to access best bid on empty bid book");
    }
    return bids_.begin()->second.front();
}

Order& OrderBook::get_best_ask_order() {
    if (asks_.empty() || asks_.begin()->second.empty()) {
        throw std::runtime_error("Attempted to access best ask on empty ask book");
    }
    return asks_.begin()->second.front();
}

const Order& OrderBook::get_best_ask_order() const {
    if (asks_.empty() || asks_.begin()->second.empty()) {
        throw std::runtime_error("Attempted to access best ask on empty ask book");
    }
    return asks_.begin()->second.front();
}

void OrderBook::pop_best_bid_order() {
    if (bids_.empty()) {
        return;
    }
    auto it = bids_.begin();
    it->second.pop_front();
    if (it->second.empty()) {
        bids_.erase(it);
    }
}

void OrderBook::pop_best_ask_order() {
    if (asks_.empty()) {
        return;
    }
    auto it = asks_.begin();
    it->second.pop_front();
    if (it->second.empty()) {
        asks_.erase(it);
    }
}

std::size_t OrderBook::total_bid_orders() const {
    std::size_t count = 0;
    for (const auto& [price, queue] : bids_) {
        (void)price;
        count += queue.size();
    }
    return count;
}

std::size_t OrderBook::total_ask_orders() const {
    std::size_t count = 0;
    for (const auto& [price, queue] : asks_) {
        (void)price;
        count += queue.size();
    }
    return count;
}

std::uint32_t OrderBook::total_bid_volume() const {
    std::uint32_t volume = 0;
    for (const auto& [price, queue] : bids_) {
        (void)price;
        for (const auto& order : queue) {
            volume += order.remaining_quantity();
        }
    }
    return volume;
}

std::uint32_t OrderBook::total_ask_volume() const {
    std::uint32_t volume = 0;
    for (const auto& [price, queue] : asks_) {
        (void)price;
        for (const auto& order : queue) {
            volume += order.remaining_quantity();
        }
    }
    return volume;
}

std::vector<Order> OrderBook::get_resting_bids() const {
    std::vector<Order> result;
    for (const auto& [price, queue] : bids_) {
        (void)price;
        for (const auto& order : queue) {
            result.push_back(order);
        }
    }
    return result;
}

std::vector<Order> OrderBook::get_resting_asks() const {
    std::vector<Order> result;
    for (const auto& [price, queue] : asks_) {
        (void)price;
        for (const auto& order : queue) {
            result.push_back(order);
        }
    }
    return result;
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
}
