#include "../engine/MarketData.hpp"
#include "../engine/Strategy.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

void test_invalid_windows() {
    std::cout << "[Test 1] Testing invalid window sizes...\n";

    // short_window == 0
    try {
        MovingAverageCrossoverStrategy s(0, 5);
        assert(false && "Should have thrown for short_window = 0");
    } catch (const std::invalid_argument& e) {
        std::cout << "  - Correctly caught: " << e.what() << "\n";
    }

    // long_window == 0
    try {
        MovingAverageCrossoverStrategy s(3, 0);
        assert(false && "Should have thrown for long_window = 0");
    } catch (const std::invalid_argument& e) {
        std::cout << "  - Correctly caught: " << e.what() << "\n";
    }

    // short_window == long_window
    try {
        MovingAverageCrossoverStrategy s(5, 5);
        assert(false && "Should have thrown for short_window == long_window");
    } catch (const std::invalid_argument& e) {
        std::cout << "  - Correctly caught: " << e.what() << "\n";
    }

    // short_window > long_window
    try {
        MovingAverageCrossoverStrategy s(10, 5);
        assert(false && "Should have thrown for short_window > long_window");
    } catch (const std::invalid_argument& e) {
        std::cout << "  - Correctly caught: " << e.what() << "\n";
    }

    std::cout << "  -> PASSED: All invalid window configurations rejected.\n\n";
}

void test_insufficient_history() {
    std::cout << "[Test 2] Testing insufficient price history...\n";
    MovingAverageCrossoverStrategy strategy(2, 4);

    // Ticks 1, 2, 3 should produce None because long_window = 4
    assert(strategy.on_tick(MarketTick{1, 100.0}) == SignalType::None);
    assert(!strategy.has_enough_data());

    assert(strategy.on_tick(MarketTick{2, 101.0}) == SignalType::None);
    assert(!strategy.has_enough_data());

    assert(strategy.on_tick(MarketTick{3, 102.0}) == SignalType::None);
    assert(!strategy.has_enough_data());

    // Tick 4 completes the history (4 ticks), but establishes baseline position -> None
    assert(strategy.on_tick(MarketTick{4, 103.0}) == SignalType::None);
    assert(strategy.has_enough_data());

    std::cout << "  -> PASSED: No signals emitted before sufficient history exists.\n\n";
}

void test_buy_crossover() {
    std::cout << "[Test 3] Testing Buy crossover...\n";
    MovingAverageCrossoverStrategy strategy(2, 4);

    // Initial setup: prices descending so short SMA < long SMA
    // Tick 1: 10.0
    // Tick 2: 10.0
    // Tick 3: 10.0
    // Tick 4:  8.0 -> Short SMA (2): 9.0, Long SMA (4): 9.5 -> Short < Long (Below)
    strategy.on_tick(MarketTick{1, 10.0});
    strategy.on_tick(MarketTick{2, 10.0});
    strategy.on_tick(MarketTick{3, 10.0});
    SignalType baseline = strategy.on_tick(MarketTick{4, 8.0});
    assert(baseline == SignalType::None);
    assert(strategy.get_latest_short_sma().value() == 9.0);
    assert(strategy.get_latest_long_sma().value() == 9.5);

    // Tick 5: Price jumps to 15.0
    // Last 4: [10.0, 10.0, 8.0, 15.0]
    // Short SMA (2): (8.0 + 15.0) / 2 = 11.5
    // Long SMA (4): (10.0 + 10.0 + 8.0 + 15.0) / 4 = 10.75
    // Short (11.5) > Long (10.75) -> Crossed from Below to Above!
    SignalType sig5 = strategy.on_tick(MarketTick{5, 15.0});
    assert(sig5 == SignalType::Buy);

    std::cout << "  -> PASSED: Buy signal correctly emitted on upward crossover.\n\n";
}

void test_no_repeated_signal() {
    std::cout << "[Test 4] Testing no repeated signal while averages remain on same side...\n";
    MovingAverageCrossoverStrategy strategy(2, 4);

    // Re-create the state from test 3:
    strategy.on_tick(MarketTick{1, 10.0});
    strategy.on_tick(MarketTick{2, 10.0});
    strategy.on_tick(MarketTick{3, 10.0});
    strategy.on_tick(MarketTick{4, 8.0});
    assert(strategy.on_tick(MarketTick{5, 15.0}) == SignalType::Buy);

    // Tick 6: Price remains high (16.0)
    // Last 4: [10.0, 8.0, 15.0, 16.0]
    // Short SMA: (15 + 16) / 2 = 15.5
    // Long SMA: (10 + 8 + 15 + 16) / 4 = 12.25
    // Short still > Long -> No new crossover -> None
    SignalType sig6 = strategy.on_tick(MarketTick{6, 16.0});
    assert(sig6 == SignalType::None);

    // Tick 7: Price remains high (17.0)
    SignalType sig7 = strategy.on_tick(MarketTick{7, 17.0});
    assert(sig7 == SignalType::None);

    std::cout << "  -> PASSED: No repeated signals while remaining on the same side.\n\n";
}

void test_sell_crossover() {
    std::cout << "[Test 5] Testing Sell crossover...\n";
    MovingAverageCrossoverStrategy strategy(2, 4);

    // Establish Above state
    strategy.on_tick(MarketTick{1, 10.0});
    strategy.on_tick(MarketTick{2, 10.0});
    strategy.on_tick(MarketTick{3, 10.0});
    strategy.on_tick(MarketTick{4, 8.0});
    assert(strategy.on_tick(MarketTick{5, 15.0}) == SignalType::Buy);
    assert(strategy.on_tick(MarketTick{6, 16.0}) == SignalType::None);

    // Tick 7: Price drops sharply to 4.0
    // Last 4: [8.0, 15.0, 16.0, 4.0]
    // Short SMA (2): (16.0 + 4.0) / 2 = 10.0
    // Long SMA (4): (8.0 + 15.0 + 16.0 + 4.0) / 4 = 10.75
    // Short (10.0) < Long (10.75) -> Crossed from Above to Below!
    SignalType sig7 = strategy.on_tick(MarketTick{7, 4.0});
    assert(sig7 == SignalType::Sell);

    // Tick 8: Price drops further to 3.0 -> remains Below -> None
    SignalType sig8 = strategy.on_tick(MarketTick{8, 3.0});
    assert(sig8 == SignalType::None);

    std::cout << "  -> PASSED: Sell signal correctly emitted on downward crossover.\n\n";
}

void test_equality_handling() {
    std::cout << "[Test 6] Testing explicit equality handling...\n";
    MovingAverageCrossoverStrategy strategy(2, 4);

    // Constant price series: SMAs will be exactly equal
    for (uint64_t i = 1; i <= 6; ++i) {
        SignalType sig = strategy.on_tick(MarketTick{i, 50.0});
        assert(sig == SignalType::None);
    }

    std::cout << "  -> PASSED: Identical averages handled without false signals.\n\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << " Running Strategy Unit Tests (Module 2) \n";
    std::cout << "========================================\n\n";

    test_invalid_windows();
    test_insufficient_history();
    test_buy_crossover();
    test_no_repeated_signal();
    test_sell_crossover();
    test_equality_handling();

    std::cout << "========================================\n";
    std::cout << " ALL STRATEGY TESTS PASSED SUCCESSFULLY! \n";
    std::cout << "========================================\n";

    return 0;
}
