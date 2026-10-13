#include "ExecutionEngine.hpp"
#include "IPCProtocol.hpp"
#include "MarketData.hpp"
#include "Order.hpp"
#include "Portfolio.hpp"
#include "RiskManager.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Standalone C++ IPC producer executable.
 * Emits newline-delimited, tab-separated protocol records exclusively to stdout,
 * while routing all diagnostic and logging output to stderr.
 */
int main(int argc, char* argv[]) {
    // Default simulation parameters for deterministic IPC run
    std::size_t total_ticks = 10;
    std::uint32_t seed = 42;
    double initial_price = 100.0;
    double starting_cash = 10000.0;
    bool force_fail = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cerr << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --ticks <N>        Number of market ticks (default: 10)\n"
                      << "  --seed <N>         Random seed for reproducibility (default: 42)\n"
                      << "  --price <P>        Starting market price (default: 100.0)\n"
                      << "  --cash <C>         Starting portfolio cash (default: 10000.0)\n"
                      << "  --fail-for-test    Simulate abnormal producer exit (for IPC error tests)\n";
            return 0;
        } else if (arg == "--ticks" && i + 1 < argc) {
            try {
                total_ticks = std::stoul(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --ticks value: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--seed" && i + 1 < argc) {
            try {
                seed = static_cast<std::uint32_t>(std::stoul(argv[++i]));
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --seed value: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--price" && i + 1 < argc) {
            try {
                initial_price = std::stod(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --price value: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--cash" && i + 1 < argc) {
            try {
                starting_cash = std::stod(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --cash value: " << argv[i] << "\n";
                return 1;
            }
        } else if (arg == "--fail-for-test") {
            force_fail = true;
        } else {
            std::cerr << "[IPC Producer] Error: Unrecognized argument: " << arg << "\n";
            return 1;
        }
    }

    if (force_fail) {
        std::cerr << "[IPC Producer] Intentional failure triggered via --fail-for-test\n";
        return 42;
    }

    std::cerr << "[IPC Producer] Starting simulation stream (ticks=" << total_ticks
              << ", seed=" << seed << ", initial_price=" << initial_price << ")\n";

    try {
        hft::ipc::ProtocolWriter writer(std::cout);

        // 1. Announce protocol version
        writer.write_version(hft::ipc::PROTOCOL_VERSION);

        // 2. Initialize simulation engine components
        MarketDataGenerator generator(initial_price, total_ticks, seed);
        ExecutionEngine execution_engine;
        Portfolio portfolio(starting_cash);
        RiskLimits limits;
        limits.max_position_quantity = 500;
        limits.max_order_quantity = 200;
        limits.max_order_notional = 25000.0;
        limits.max_gross_exposure = 50000.0;
        RiskManager risk_manager(limits);

        // Emit initial portfolio snapshot
        writer.write_portfolio(portfolio, initial_price);

        std::size_t ticks_emitted = 0;
        std::size_t trades_emitted = 0;
        std::size_t portfolio_updates_emitted = 1; // initial state
        OrderId next_order_id = 1;

        while (generator.has_next_tick()) {
            MarketTick tick = generator.next_tick();
            ++ticks_emitted;

            // Emit tick update
            writer.write_tick(tick.tick_number, tick.price);

            // Deterministic trading demonstration:
            // On tick 3: Resting sell order is placed by market maker, then strategy places buy order
            if (tick.tick_number == 3) {
                // Seller places limit order to sell 50 units @ current tick price
                Order sell_order = Order::create_limit(next_order_id++, OrderSide::Sell, 50, tick.price);
                execution_engine.submit_order(sell_order);

                // Buyer proposes market order for 30 units
                Order buy_order = Order::create_market(next_order_id++, OrderSide::Buy, 30);
                auto risk_res = risk_manager.check_order(buy_order, portfolio, tick.price);
                if (risk_res.approved) {
                    auto exec_res = execution_engine.submit_order(buy_order);
                    for (const auto& trade : exec_res.trades) {
                        ++trades_emitted;
                        writer.write_trade(trade);
                        portfolio.apply_trade(trade, OrderSide::Buy);
                        ++portfolio_updates_emitted;
                        writer.write_portfolio(portfolio, tick.price);
                    }
                }
            }

            // On tick 7: Strategy sells 10 units of its position
            if (tick.tick_number == 7 && portfolio.get_position_quantity() >= 10) {
                // Buyer places resting buy order at tick price
                Order resting_buy = Order::create_limit(next_order_id++, OrderSide::Buy, 20, tick.price);
                execution_engine.submit_order(resting_buy);

                // Strategy sells 10 units
                Order sell_order = Order::create_market(next_order_id++, OrderSide::Sell, 10);
                auto risk_res = risk_manager.check_order(sell_order, portfolio, tick.price);
                if (risk_res.approved) {
                    auto exec_res = execution_engine.submit_order(sell_order);
                    for (const auto& trade : exec_res.trades) {
                        ++trades_emitted;
                        writer.write_trade(trade);
                        portfolio.apply_trade(trade, OrderSide::Sell);
                        ++portfolio_updates_emitted;
                        writer.write_portfolio(portfolio, tick.price);
                    }
                }
            }
        }

        // 3. Emit clean completion message
        writer.write_end(hft::ipc::PROTOCOL_VERSION);

        std::cerr << "[IPC Producer] Finished simulation successfully. Summary: "
                  << ticks_emitted << " ticks, "
                  << trades_emitted << " trades, "
                  << portfolio_updates_emitted << " portfolio updates.\n";

        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "[IPC Producer] Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
