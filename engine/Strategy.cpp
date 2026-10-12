#include "Strategy.hpp"

#include <numeric>
#include <stdexcept>
#include <string>

std::string to_string(SignalType signal) {
    switch (signal) {
        case SignalType::Buy:
            return "BUY";
        case SignalType::Sell:
            return "SELL";
        case SignalType::None:
            return "NONE";
    }
    return "NONE";
}

MovingAverageCrossoverStrategy::MovingAverageCrossoverStrategy(std::size_t short_window,
                                                               std::size_t long_window)
    : short_window_(short_window), long_window_(long_window) {
    if (short_window == 0) {
        throw std::invalid_argument("Strategy error: short_window must be greater than 0");
    }
    if (long_window == 0) {
        throw std::invalid_argument("Strategy error: long_window must be greater than 0");
    }
    if (short_window >= long_window) {
        throw std::invalid_argument(
            "Strategy error: short_window (" + std::to_string(short_window) +
            ") must be strictly less than long_window (" + std::to_string(long_window) + ")");
    }
}

void MovingAverageCrossoverStrategy::reset() {
    prices_.clear();
    previous_position_ = RelativePosition::Unknown;
}

double MovingAverageCrossoverStrategy::calculate_sma(std::size_t window) const {
    auto start_it = prices_.end() - window;
    double sum = std::accumulate(start_it, prices_.end(), 0.0);
    return sum / static_cast<double>(window);
}

std::optional<double> MovingAverageCrossoverStrategy::get_latest_short_sma() const {
    if (prices_.size() < short_window_) {
        return std::nullopt;
    }
    return calculate_sma(short_window_);
}

std::optional<double> MovingAverageCrossoverStrategy::get_latest_long_sma() const {
    if (prices_.size() < long_window_) {
        return std::nullopt;
    }
    return calculate_sma(long_window_);
}

SignalType MovingAverageCrossoverStrategy::on_tick(const MarketTick& tick) {
    prices_.push_back(tick.price);
    if (prices_.size() > long_window_) {
        prices_.pop_front();
    }

    // Do not emit signals until enough history exists for both SMAs
    if (prices_.size() < long_window_) {
        return SignalType::None;
    }

    double short_sma = calculate_sma(short_window_);
    double long_sma = calculate_sma(long_window_);
    double diff = short_sma - long_sma;

    SignalType signal = SignalType::None;

    if (diff > EPSILON) {
        // Short SMA is strictly above Long SMA
        if (previous_position_ == RelativePosition::Below) {
            signal = SignalType::Buy;
        }
        previous_position_ = RelativePosition::Above;
    } else if (diff < -EPSILON) {
        // Short SMA is strictly below Long SMA
        if (previous_position_ == RelativePosition::Above) {
            signal = SignalType::Sell;
        }
        previous_position_ = RelativePosition::Below;
    } else {
        // Explicit equality handling: within floating-point epsilon
        if (previous_position_ == RelativePosition::Unknown) {
            previous_position_ = RelativePosition::Equal;
        }
        // Equality does not trigger a crossover signal
    }

    return signal;
}
