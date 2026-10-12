#include "MarketData.hpp"
#include <algorithm>

MarketDataGenerator::MarketDataGenerator(double initial_price,
                                         std::size_t total_ticks,
                                         std::optional<uint32_t> seed)
    : initial_price_(std::max(initial_price, MIN_PRICE)),
      current_price_(std::max(initial_price, MIN_PRICE)),
      total_ticks_(total_ticks),
      current_tick_(0),
      seed_(seed),
      step_dist_(-MAX_STEP, MAX_STEP) {
    reset();
}

void MarketDataGenerator::reset() {
    current_price_ = initial_price_;
    current_tick_ = 0;

    if (seed_.has_value()) {
        rng_.seed(seed_.value());
    } else {
        std::random_device rd;
        rng_.seed(rd());
    }
}

bool MarketDataGenerator::has_next_tick() const {
    return current_tick_ < total_ticks_;
}

MarketTick MarketDataGenerator::next_tick() {
    if (!has_next_tick()) {
        return MarketTick{current_tick_, current_price_};
    }

    ++current_tick_;

    // Random walk formula:
    // price_t = max(MIN_PRICE, price_{t-1} + delta)
    // where delta is uniformly distributed in [-MAX_STEP, +MAX_STEP]
    double delta = step_dist_(rng_);
    current_price_ += delta;

    // Guard against zero or negative prices
    if (current_price_ < MIN_PRICE) {
        current_price_ = MIN_PRICE;
    }

    return MarketTick{current_tick_, current_price_};
}

std::vector<MarketTick> MarketDataGenerator::generate_all() {
    std::vector<MarketTick> ticks;
    ticks.reserve(total_ticks_);
    while (has_next_tick()) {
        ticks.push_back(next_tick());
    }
    return ticks;
}
