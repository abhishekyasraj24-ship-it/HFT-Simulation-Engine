#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

/**
 * @brief Represents a single price update in the simulated market feed.
 */
struct MarketTick {
    uint64_t tick_number; ///< Sequential tick index (1, 2, 3...)
    double price;         ///< Simulated price at this tick
};

/**
 * @brief Simple random-walk market data generator for simulation.
 *
 * NOTE: This is an educational simulation component and does not reflect
 * real financial market dynamics or production exchange feeds.
 */
class MarketDataGenerator {
public:
    /**
     * @brief Constructs a new MarketDataGenerator.
     * @param initial_price Starting price (defaults to 100.0).
     * @param total_ticks Number of ticks to generate (defaults to 20).
     * @param seed Optional seed for reproducible random walks.
     */
    explicit MarketDataGenerator(double initial_price = 100.0,
                                 std::size_t total_ticks = 20,
                                 std::optional<uint32_t> seed = std::nullopt);

    /**
     * @brief Checks if there are more ticks to generate.
     */
    bool has_next_tick() const;

    /**
     * @brief Generates and returns the next MarketTick.
     * Advances the tick counter and updates the price.
     */
    MarketTick next_tick();

    /**
     * @brief Generates all remaining ticks as a batch vector.
     */
    std::vector<MarketTick> generate_all();

    /**
     * @brief Resets the generator back to the initial price, tick 0,
     * and resets the random engine state.
     */
    void reset();

    // Accessors
    double get_initial_price() const { return initial_price_; }
    double get_current_price() const { return current_price_; }
    std::size_t get_total_ticks() const { return total_ticks_; }
    uint64_t get_current_tick() const { return current_tick_; }

private:
    double initial_price_;
    double current_price_;
    std::size_t total_ticks_;
    uint64_t current_tick_;
    std::optional<uint32_t> seed_;

    std::mt19937 rng_;
    std::uniform_real_distribution<double> step_dist_;

    // Minimum allowed price to prevent zero or negative values
    static constexpr double MIN_PRICE = 0.01;
    // Maximum price change per tick: [-0.50, +0.50]
    static constexpr double MAX_STEP = 0.50;
};
