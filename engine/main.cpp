#include "ExecutionEngine.hpp"
#include "MarketData.hpp"
#include "Portfolio.hpp"
#include "RiskManager.hpp"
#include "Strategy.hpp"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

int main(int argc, char* argv[]) {
    // Default configuration
    double initial_price = 100.0;
    std::size_t total_ticks = 20;
    std::optional<uint32_t> seed = std::nullopt;

    // Command-line arguments:
    // Usage: ./market_data [initial_price] [total_ticks] [seed]
    if (argc > 1) {
        std::string arg1 = argv[1];
        if (arg1 == "-h" || arg1 == "--help") {
            std::cout << "Usage: " << argv[0] << " [initial_price] [total_ticks] [seed]\n\n"
                      << "Arguments:\n"
                      << "  initial_price   Starting price (default: 100.0)\n"
                      << "  total_ticks     Total number of ticks to generate (default: 20)\n"
                      << "  seed            Integer seed for reproducible runs (optional)\n";
            return 0;
        }

        try {
            initial_price = std::stod(argv[1]);
        } catch (const std::exception&) {
            std::cerr << "Error: Invalid initial_price value: " << argv[1] << "\n";
            return 1;
        }
    }

    if (argc > 2) {
        try {
            total_ticks = std::stoul(argv[2]);
        } catch (const std::exception&) {
            std::cerr << "Error: Invalid total_ticks value: " << argv[2] << "\n";
            return 1;
        }
    }

    if (argc > 3) {
        try {
            seed = static_cast<uint32_t>(std::stoul(argv[3]));
        } catch (const std::exception&) {
            std::cerr << "Error: Invalid seed value: " << argv[3] << "\n";
            return 1;
        }
    }

    // Initialize market data generator and strategy demo (short window: 3, long window: 5)
    constexpr std::size_t SHORT_WINDOW = 3;
    constexpr std::size_t LONG_WINDOW = 5;
    MarketDataGenerator generator(initial_price, total_ticks, seed);
    MovingAverageCrossoverStrategy strategy(SHORT_WINDOW, LONG_WINDOW);

    std::cout << "========================================================\n";
    std::cout << "   HFT Simulation: Market Data & Strategy Engine\n";
    std::cout << "========================================================\n";
    std::cout << "Configuration:\n";
    std::cout << "  Initial Price: $" << std::fixed << std::setprecision(2) << initial_price << "\n";
    std::cout << "  Total Ticks:   " << total_ticks << "\n";
    if (seed.has_value()) {
        std::cout << "  Random Seed:   " << seed.value() << " (reproducible)\n";
    } else {
        std::cout << "  Random Seed:   None (non-deterministic)\n";
    }
    std::cout << "  Strategy:      SMA Crossover (Short: " << SHORT_WINDOW
              << ", Long: " << LONG_WINDOW << ")\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "Streaming Ticks and Strategy Signals:\n";
    std::cout << "--------------------------------------------------------\n";

    std::size_t buy_signals = 0;
    std::size_t sell_signals = 0;

    while (generator.has_next_tick()) {
        MarketTick tick = generator.next_tick();
        SignalType signal = strategy.on_tick(tick);

        if (signal == SignalType::Buy) {
            ++buy_signals;
        } else if (signal == SignalType::Sell) {
            ++sell_signals;
        }

        std::cout << "Tick " << std::setw(4) << std::setfill('0') << tick.tick_number
                  << " | Price: $" << std::fixed << std::setprecision(2) << std::setfill(' ') << std::setw(7)
                  << tick.price
                  << " | Signal: " << std::setw(4) << to_string(signal) << "\n";
    }

    std::cout << "========================================================\n";
    std::cout << "Simulation completed (" << total_ticks << " ticks generated).\n";
    std::cout << "Summary: " << buy_signals << " BUY signals, "
              << sell_signals << " SELL signals.\n";
    std::cout << "========================================================\n";

    // --- Module 4 Demonstration: Simulated Order Execution ---
    std::cout << "\n========================================================\n";
    std::cout << "   Module 4 Demonstration: Order Book & Execution Engine\n";
    std::cout << "========================================================\n";
    ExecutionEngine engine;

    std::cout << "1. Seeding Order Book with resting limit orders:\n";
    Order ask1 = Order::create_limit(101, OrderSide::Sell, 50, 101.50);
    Order ask2 = Order::create_limit(102, OrderSide::Sell, 50, 102.00);
    Order bid1 = Order::create_limit(103, OrderSide::Buy, 50, 98.50);

    engine.submit_order(ask1);
    engine.submit_order(ask2);
    engine.submit_order(bid1);

    std::cout << "   - Resting Asks: 50 @ $101.50, 50 @ $102.00\n";
    std::cout << "   - Resting Bids: 50 @ $98.50\n";
    std::cout << "   - Best Bid: $" << std::fixed << std::setprecision(2)
              << engine.get_order_book().get_best_bid_price().value()
              << " | Best Ask: $" << engine.get_order_book().get_best_ask_price().value() << "\n";

    std::cout << "\n2. Submitting aggressive Market Buy order for 75 units:\n";
    Order market_buy{201, OrderSide::Buy, OrderType::Market, 75, std::nullopt, OrderStatus::New, 0};
    auto exec_res = engine.submit_order(market_buy);

    std::cout << "   - Executed Trades (" << exec_res.trades.size() << "):\n";
    for (const auto& trade : exec_res.trades) {
        std::cout << "     * " << trade.to_string_summary() << "\n";
    }
    std::cout << "   - Market Order Status: " << to_string(exec_res.order.status)
              << " (Filled: " << exec_res.order.filled_quantity << "/" << exec_res.order.quantity << ")\n";
    std::cout << "   - Remaining Best Ask in Book: $"
              << engine.get_order_book().get_best_ask_price().value()
              << " (" << engine.get_order_book().total_ask_volume() << " units available)\n";
    std::cout << "========================================================\n";

    // --- Module 5 Demonstration: Portfolio & P&L ---
    std::cout << "\n========================================================\n";
    std::cout << "   Module 5 Demonstration: Portfolio & P&L Management\n";
    std::cout << "========================================================\n";

    Portfolio portfolio(20000.0);
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Starting cash: $" << portfolio.get_cash() << "\n\n";

    // Feed the trades from the market-buy execution above into the portfolio.
    // The portfolio takes the buyer's perspective on every trade.
    std::cout << "Applying executed trades from Module 4 demo (buyer's view):\n";
    for (const auto& trade : exec_res.trades) {
        bool ok = portfolio.process_trade(trade, OrderSide::Buy);
        if (ok) {
            std::cout << "  + Applied: " << trade.to_string_summary() << "\n";
        } else {
            std::cout << "  ! Rejected: " << portfolio.get_last_error() << "\n";
        }
    }

    // Snapshot at a hypothetical mark-to-market price of $103.00
    constexpr double mark_price = 103.00;
    std::cout << "\nPortfolio Snapshot (mark-to-market @ $" << mark_price << "):\n";
    std::cout << "  Cash             : $" << portfolio.get_cash() << "\n";
    std::cout << "  Position Qty     : " << portfolio.get_position_quantity() << " units\n";
    std::cout << "  Avg Entry Price  : $" << portfolio.get_average_entry_price() << "\n";
    std::cout << "  Realized P&L     : $" << portfolio.get_realized_pnl() << "\n";
    std::cout << "  Unrealized P&L   : $" << portfolio.get_unrealized_pnl(mark_price) << "\n";
    std::cout << "  Total P&L        : $" << portfolio.get_total_pnl(mark_price) << "\n";
    std::cout << "  Total Equity     : $" << portfolio.get_total_equity(mark_price) << "\n";
    std::cout << "========================================================\n";

    // --- Module 6 Demonstration: Risk Management System ---
    std::cout << "\n========================================================\n";
    std::cout << "   Module 6 Demonstration: Pre-Trade Risk Management\n";
    std::cout << "========================================================\n";

    // Configure strict risk limits for demonstration purposes:
    //   - Max 50 units per order
    //   - Max $5,500 notional per order
    //   - Max 100 units total position
    //   - Max $11,000 gross exposure
    RiskLimits demo_limits;
    demo_limits.max_order_quantity    = 50;
    demo_limits.max_order_notional    = 5500.0;
    demo_limits.max_position_quantity = 100;
    demo_limits.max_gross_exposure    = 11000.0;

    RiskManager risk_mgr(demo_limits);

    // Use a fresh portfolio starting flat with $50,000 cash
    Portfolio risk_portfolio(50000.0);
    constexpr double ref_price = 110.0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Risk Limits:\n"
              << "  max_order_quantity    : " << demo_limits.max_order_quantity << " units\n"
              << "  max_order_notional    : $" << demo_limits.max_order_notional << "\n"
              << "  max_position_quantity : " << demo_limits.max_position_quantity << " units\n"
              << "  max_gross_exposure    : $" << demo_limits.max_gross_exposure << "\n"
              << "  Reference price       : $" << ref_price << "\n\n";

    // Helper lambda to print a check result
    auto print_check = [](const std::string& label, const Order& ord,
                          const RiskCheckResult& result) {
        std::cout << "  Order: " << ord.to_string_summary() << "\n";
        if (result.approved) {
            std::cout << "  [" << label << "] APPROVED\n\n";
        } else {
            std::cout << "  [" << label << "] REJECTED: " << result.rejection_reason << "\n\n";
        }
    };

    // 1. Approved: buy 50 @ $100 limit → notional $5000 ≤ $5500, position 50 ≤ 100
    Order risk_order_1 = Order::create_limit(301, OrderSide::Buy, 50, 100.0);
    auto  risk_res_1   = risk_mgr.check_order(risk_order_1, risk_portfolio, ref_price);
    print_check("CASE 1 — within all limits", risk_order_1, risk_res_1);

    // 2. Rejected: buy 50 @ $111 limit → notional 50*111=$5550 > $5500
    Order risk_order_2 = Order::create_limit(302, OrderSide::Buy, 50, 111.0);
    auto  risk_res_2   = risk_mgr.check_order(risk_order_2, risk_portfolio, ref_price);
    print_check("CASE 2 — notional exceeds limit", risk_order_2, risk_res_2);

    // 3. Rejected: market buy 101 units → exceeds max_order_quantity (50)
    Order risk_order_3 = Order::create_market(303, OrderSide::Buy, 51);
    auto  risk_res_3   = risk_mgr.check_order(risk_order_3, risk_portfolio, ref_price);
    print_check("CASE 3 — order quantity exceeds limit", risk_order_3, risk_res_3);

    // 4. Rejected: sell on flat portfolio → long-only portfolio, no position to sell
    Order risk_order_4 = Order::create_market(304, OrderSide::Sell, 10);
    auto  risk_res_4   = risk_mgr.check_order(risk_order_4, risk_portfolio, ref_price);
    print_check("CASE 4 — sell with no position (long-only)", risk_order_4, risk_res_4);

    // Confirm portfolio state is completely untouched by all the risk checks
    assert(risk_portfolio.get_position_quantity() == 0);
    assert(risk_portfolio.get_cash() == 50000.0);
    std::cout << "  Portfolio state after all risk checks: cash=$"
              << risk_portfolio.get_cash()
              << ", position=" << risk_portfolio.get_position_quantity() << " units\n";
    std::cout << "  (Unchanged — risk checks are read-only)\n";
    std::cout << "========================================================\n";

    return 0;
}
