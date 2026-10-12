#pragma once

#include "MarketData.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>

/**
 * @brief Trading signal emitted by a strategy.
 */
enum class SignalType {
    None, ///< No signal (or holding current state)
    Buy,  ///< Buy signal triggered (bullish crossover)
    Sell  ///< Sell signal triggered (bearish crossover)
};

/**
 * @brief Converts a SignalType enum to a human-readable string.
 */
std::string to_string(SignalType signal);

/**
 * @brief Simple Moving Average (SMA) crossover trading strategy.
 *
 * - Calculates short-window and long-window simple moving averages.
 * - Emits Buy when short SMA crosses from below to above long SMA.
 * - Emits Sell when short SMA crosses from above to below long SMA.
 * - Emits None otherwise (or when history is insufficient).
 */
class MovingAverageCrossoverStrategy {
public:
    /**
     * @brief Constructs an SMA Crossover Strategy.
     * @param short_window Size of the short moving average window.
     * @param long_window Size of the long moving average window.
     * @throws std::invalid_argument if short_window == 0, long_window == 0,
     *         or short_window >= long_window.
     */
    MovingAverageCrossoverStrategy(std::size_t short_window, std::size_t long_window);

    /**
     * @brief Processes a new market tick and returns any resulting signal.
     * @param tick The incoming price tick.
     * @return SignalType The signal generated for this tick (Buy, Sell, or None).
     */
    SignalType on_tick(const MarketTick& tick);

    /**
     * @brief Resets the strategy's observed price history and crossover state.
     */
    void reset();

    // Inspection accessors
    std::size_t get_short_window() const { return short_window_; }
    std::size_t get_long_window() const { return long_window_; }
    bool has_enough_data() const { return prices_.size() >= long_window_; }

    std::optional<double> get_latest_short_sma() const;
    std::optional<double> get_latest_long_sma() const;

private:
    enum class RelativePosition {
        Unknown, ///< Not enough data to determine position
        Below,   ///< Short SMA < Long SMA
        Above,   ///< Short SMA > Long SMA
        Equal    ///< Short SMA == Long SMA
    };

    std::size_t short_window_;
    std::size_t long_window_;
    std::deque<double> prices_;
    RelativePosition previous_position_{RelativePosition::Unknown};

    double calculate_sma(std::size_t window) const;

    // Small epsilon threshold for floating-point comparisons
    static constexpr double EPSILON = 1e-9;
};
