#include "ExecutionEngine.hpp"
#include "IPCProtocol.hpp"
#include "MarketData.hpp"
#include "Order.hpp"
#include "Portfolio.hpp"
#include "RiskManager.hpp"

#include <cmath>
#include <iostream>
#include <limits>
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
    std::uint32_t max_order_qty = 200;
    std::uint32_t max_position_qty = 500;
    bool force_fail = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cerr << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --ticks <N>          Number of market ticks (default: 10)\n"
                      << "  --seed <N>           Random seed for reproducibility (default: 42)\n"
                      << "  --price <P>          Starting market price (default: 100.0)\n"
                      << "  --cash <C>           Starting portfolio cash (default: 10000.0)\n"
                      << "  --max-order-qty <Q>  Maximum units per order for risk check (default: 200)\n"
                      << "  --max-position <P>   Maximum position units for risk check (default: 500)\n"
                      << "  --fail-for-test      Simulate abnormal producer exit (for IPC error tests)\n";
            return 0;
        } else if (arg == "--ticks" && i + 1 < argc) {
            std::string val_str = argv[++i];
            try {
                std::size_t pos = 0;
                long long val = std::stoll(val_str, &pos);
                if (pos != val_str.size()) {
                    std::cerr << "[IPC Producer] Error: Invalid --ticks value: " << val_str << "\n";
                    return 1;
                }
                if (val < 0) {
                    std::cerr << "[IPC Producer] Error: --ticks cannot be negative: " << val << "\n";
                    return 1;
                }
                total_ticks = static_cast<std::size_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --ticks value: " << val_str << "\n";
                return 1;
            }
        } else if (arg == "--seed" && i + 1 < argc) {
            std::string val_str = argv[++i];
            try {
                std::size_t pos = 0;
                long long val = std::stoll(val_str, &pos);
                if (pos != val_str.size()) {
                    std::cerr << "[IPC Producer] Error: Invalid --seed value: " << val_str << "\n";
                    return 1;
                }
                if (val < 0) {
                    std::cerr << "[IPC Producer] Error: --seed cannot be negative: " << val << "\n";
                    return 1;
                }
                if (val > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
                    std::cerr << "[IPC Producer] Error: --seed exceeds 32-bit limit ("
                              << std::numeric_limits<std::uint32_t>::max() << "): " << val << "\n";
                    return 1;
                }
                seed = static_cast<std::uint32_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --seed value: " << val_str << "\n";
                return 1;
            }
        } else if (arg == "--price" && i + 1 < argc) {
            std::string val_str = argv[++i];
            try {
                std::size_t pos = 0;
                double val = std::stod(val_str, &pos);
                if (pos != val_str.size()) {
                    std::cerr << "[IPC Producer] Error: Invalid --price value: " << val_str << "\n";
                    return 1;
                }
                if (val <= 0.0 || !std::isfinite(val)) {
                    std::cerr << "[IPC Producer] Error: --price must be strictly positive and finite: " << val_str << "\n";
                    return 1;
                }
                initial_price = val;
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --price value: " << val_str << "\n";
                return 1;
            }
        } else if (arg == "--cash" && i + 1 < argc) {
            std::string val_str = argv[++i];
            try {
                std::size_t pos = 0;
                double val = std::stod(val_str, &pos);
                if (pos != val_str.size()) {
                    std::cerr << "[IPC Producer] Error: Invalid --cash value: " << val_str << "\n";
                    return 1;
                }
                if (val < 0.0 || !std::isfinite(val)) {
                    std::cerr << "[IPC Producer] Error: --cash cannot be negative: " << val_str << "\n";
                    return 1;
                }
                starting_cash = val;
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --cash value: " << val_str << "\n";
                return 1;
            }
        } else if (arg == "--max-order-qty" && i + 1 < argc) {
            std::string val_str = argv[++i];
            try {
                std::size_t pos = 0;
                long long val = std::stoll(val_str, &pos);
                if (pos != val_str.size()) {
                    std::cerr << "[IPC Producer] Error: Invalid --max-order-qty value: " << val_str << "\n";
                    return 1;
                }
                if (val <= 0) {
                    std::cerr << "[IPC Producer] Error: --max-order-qty must be strictly positive: " << val << "\n";
                    return 1;
                }
                if (val > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
                    std::cerr << "[IPC Producer] Error: --max-order-qty exceeds 32-bit limit ("
                              << std::numeric_limits<std::uint32_t>::max() << "): " << val << "\n";
                    return 1;
                }
                max_order_qty = static_cast<std::uint32_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --max-order-qty value: " << val_str << "\n";
                return 1;
            }
        } else if (arg == "--max-position" && i + 1 < argc) {
            std::string val_str = argv[++i];
            try {
                std::size_t pos = 0;
                long long val = std::stoll(val_str, &pos);
                if (pos != val_str.size()) {
                    std::cerr << "[IPC Producer] Error: Invalid --max-position value: " << val_str << "\n";
                    return 1;
                }
                if (val <= 0) {
                    std::cerr << "[IPC Producer] Error: --max-position must be strictly positive: " << val << "\n";
                    return 1;
                }
                if (val > static_cast<long long>(std::numeric_limits<std::uint32_t>::max())) {
                    std::cerr << "[IPC Producer] Error: --max-position exceeds 32-bit limit ("
                              << std::numeric_limits<std::uint32_t>::max() << "): " << val << "\n";
                    return 1;
                }
                max_position_qty = static_cast<std::uint32_t>(val);
            } catch (const std::exception&) {
                std::cerr << "[IPC Producer] Error: Invalid --max-position value: " << val_str << "\n";
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
        limits.max_position_quantity = max_position_qty;
        limits.max_order_quantity = max_order_qty;
        limits.max_order_notional = 25000.0;
        limits.max_gross_exposure = 50000.0;
        RiskManager risk_manager(limits);

        // Emit initial portfolio snapshot
        writer.write_portfolio(portfolio, initial_price);

        std::size_t ticks_emitted = 0;
        std::size_t trades_emitted = 0;
        std::size_t portfolio_updates_emitted = 1; // initial state
        OrderId next_order_id = 1;

        while (generator.has_next_tick() && std::cout.good()) {
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
                    }
                } else {
                    std::cerr << "[IPC Producer] Risk check rejected order #" << buy_order.id
                              << ": " << risk_res.rejection_reason << "\n";
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
                    }
                } else {
                    std::cerr << "[IPC Producer] Risk check rejected order #" << sell_order.id
                              << ": " << risk_res.rejection_reason << "\n";
                }
            }

            // Emit mark-to-market portfolio snapshot after processing each market tick
            ++portfolio_updates_emitted;
            writer.write_portfolio(portfolio, tick.price);
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
