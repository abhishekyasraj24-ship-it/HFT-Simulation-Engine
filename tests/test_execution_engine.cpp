#include "../engine/ExecutionEngine.hpp"
#include "../engine/Order.hpp"
#include "../engine/Trade.hpp"

#include <cassert>
#include <iostream>

void test_matching_buy_sell() {
    std::cout << "[Test 1] Testing basic Buy and Sell order matching...\n";
    ExecutionEngine engine;

    // Resting Sell Limit order: 100 units @ $100.00
    Order sell_order = Order::create_limit(1, OrderSide::Sell, 100, 100.00);
    auto res_sell = engine.submit_order(sell_order);
    assert(res_sell.trades.empty());
    assert(res_sell.order.status == OrderStatus::New);
    assert(engine.get_order_book().total_ask_volume() == 100);

    // Incoming Buy Limit order: 100 units @ $100.00
    Order buy_order = Order::create_limit(2, OrderSide::Buy, 100, 100.00);
    auto res_buy = engine.submit_order(buy_order);

    assert(res_buy.trades.size() == 1);
    assert(res_buy.order.status == OrderStatus::Filled);
    assert(res_buy.order.remaining_quantity() == 0);
    assert(res_buy.order.filled_quantity == 100);

    const Trade& t = res_buy.trades[0];
    assert(t.buy_order_id == 2);
    assert(t.sell_order_id == 1);
    assert(t.price == 100.00);
    assert(t.quantity == 100);

    assert(engine.get_order_book().empty());
    assert(engine.get_trade_history().size() == 1);

    std::cout << "  - " << t.to_string_summary() << "\n";
    std::cout << "  -> PASSED: Buy and sell matched with correct trade record.\n\n";
}

void test_non_crossing_orders_remain_in_book() {
    std::cout << "[Test 2] Testing non-crossing orders resting in book...\n";
    ExecutionEngine engine;

    // Bid @ 99.00 and Ask @ 101.00 should not cross
    Order buy_order = Order::create_limit(10, OrderSide::Buy, 50, 99.00);
    Order sell_order = Order::create_limit(11, OrderSide::Sell, 50, 101.00);

    auto res1 = engine.submit_order(buy_order);
    auto res2 = engine.submit_order(sell_order);

    assert(res1.trades.empty());
    assert(res2.trades.empty());
    assert(res1.order.status == OrderStatus::New);
    assert(res2.order.status == OrderStatus::New);

    const auto& book = engine.get_order_book();
    assert(book.has_bids() && book.has_asks());
    assert(book.get_best_bid_price().value() == 99.00);
    assert(book.get_best_ask_price().value() == 101.00);
    assert(book.total_bid_volume() == 50);
    assert(book.total_ask_volume() == 50);

    std::cout << "  -> PASSED: Non-crossing orders correctly rest on both sides of the book.\n\n";
}

void test_price_time_priority() {
    std::cout << "[Test 3] Testing Price-Time Priority...\n";
    ExecutionEngine engine;

    // Resting asks:
    // Order 20: 10 units @ 102.00 (arrived first)
    // Order 21: 10 units @ 100.00 (arrived second, better price)
    // Order 22: 10 units @ 100.00 (arrived third, same price as 21)
    engine.submit_order(Order::create_limit(20, OrderSide::Sell, 10, 102.00));
    engine.submit_order(Order::create_limit(21, OrderSide::Sell, 10, 100.00));
    engine.submit_order(Order::create_limit(22, OrderSide::Sell, 10, 100.00));

    // Incoming Buy order: 15 units @ 101.00
    // Should match:
    // 1. Order 21 first (best price 100.00, arrived earlier): 10 units
    // 2. Order 22 second (best price 100.00, arrived later): 5 units
    // 3. Order 20 (102.00) should NOT trade because price 102.00 > buyer limit 101.00.
    Order buyer = Order::create_limit(23, OrderSide::Buy, 15, 101.00);
    auto res = engine.submit_order(buyer);

    assert(res.trades.size() == 2);
    assert(res.trades[0].sell_order_id == 21);
    assert(res.trades[0].quantity == 10);
    assert(res.trades[0].price == 100.00);

    assert(res.trades[1].sell_order_id == 22);
    assert(res.trades[1].quantity == 5);
    assert(res.trades[1].price == 100.00);

    assert(res.order.status == OrderStatus::Filled);

    // Book state verification:
    // Order 22 should have 5 units remaining
    // Order 20 should have 10 units remaining
    const auto& book = engine.get_order_book();
    assert(book.total_ask_volume() == 15);
    assert(book.get_best_ask_price().value() == 100.00);

    std::cout << "  -> PASSED: Price-time priority respected across multiple price levels and queue positions.\n\n";
}

void test_partial_fills() {
    std::cout << "[Test 4] Testing partial fills and remaining quantities...\n";
    ExecutionEngine engine;

    // Resting sell order: 100 units @ 50.00
    engine.submit_order(Order::create_limit(30, OrderSide::Sell, 100, 50.00));

    // Incoming buy order: 40 units @ 50.00
    auto res = engine.submit_order(Order::create_limit(31, OrderSide::Buy, 40, 50.00));

    assert(res.trades.size() == 1);
    assert(res.trades[0].quantity == 40);
    assert(res.order.status == OrderStatus::Filled);
    assert(res.order.filled_quantity == 40);
    assert(res.order.remaining_quantity() == 0);

    // Resting order in book should have status PartiallyFilled and 60 remaining
    const Order& resting = engine.get_order_book().get_best_ask_order();
    assert(resting.id == 30);
    assert(resting.filled_quantity == 40);
    assert(resting.remaining_quantity() == 60);
    assert(resting.status == OrderStatus::PartiallyFilled);
    assert(engine.get_order_book().total_ask_volume() == 60);

    std::cout << "  -> PASSED: Partial fill correctly updated resting and incoming quantities.\n\n";
}

void test_multiple_trades_from_single_order() {
    std::cout << "[Test 5] Testing multiple trades sweeping the book from one incoming order...\n";
    ExecutionEngine engine;

    // Add three asks
    engine.submit_order(Order::create_limit(40, OrderSide::Sell, 20, 10.00));
    engine.submit_order(Order::create_limit(41, OrderSide::Sell, 30, 11.00));
    engine.submit_order(Order::create_limit(42, OrderSide::Sell, 50, 12.00));

    // Sweep 75 units using Market order
    Order market_buy{43, OrderSide::Buy, OrderType::Market, 75, std::nullopt, OrderStatus::New, 0};
    auto res = engine.submit_order(market_buy);

    assert(res.trades.size() == 3);
    assert(res.trades[0].quantity == 20 && res.trades[0].price == 10.00);
    assert(res.trades[1].quantity == 30 && res.trades[1].price == 11.00);
    assert(res.trades[2].quantity == 25 && res.trades[2].price == 12.00);

    assert(res.order.status == OrderStatus::Filled);
    assert(res.order.filled_quantity == 75);
    assert(res.order.remaining_quantity() == 0);

    // Order 42 should have 25 units left @ 12.00
    assert(engine.get_order_book().total_ask_volume() == 25);
    assert(engine.get_order_book().get_best_ask_price().value() == 12.00);

    std::cout << "  -> PASSED: Single order swept multiple price levels and generated 3 trade records.\n\n";
}

void test_market_orders_insufficient_liquidity() {
    std::cout << "[Test 6] Testing Market orders with insufficient liquidity...\n";
    ExecutionEngine engine;

    // Resting liquidity: only 30 units available
    engine.submit_order(Order::create_limit(50, OrderSide::Sell, 30, 100.00));

    // Incoming Market Buy for 50 units
    Order market_buy{51, OrderSide::Buy, OrderType::Market, 50, std::nullopt, OrderStatus::New, 0};
    auto res = engine.submit_order(market_buy);

    assert(res.trades.size() == 1);
    assert(res.trades[0].quantity == 30);
    assert(res.order.filled_quantity == 30);
    assert(res.order.remaining_quantity() == 20);
    // Unfilled remainder of 20 was cancelled
    assert(res.order.status == OrderStatus::Cancelled);

    // Book is now completely empty
    assert(engine.get_order_book().empty());

    std::cout << "  -> PASSED: Market order filled available liquidity and cancelled remainder.\n\n";
}

void test_invalid_orders_and_empty_book() {
    std::cout << "[Test 7] Testing invalid orders and empty-book handling...\n";
    ExecutionEngine engine;

    // 1. Market order on empty book
    Order empty_book_order{60, OrderSide::Buy, OrderType::Market, 100, std::nullopt, OrderStatus::New, 0};
    auto res_empty = engine.submit_order(empty_book_order);
    assert(res_empty.trades.empty());
    assert(res_empty.order.status == OrderStatus::Cancelled);
    assert(res_empty.order.filled_quantity == 0);

    // 2. Quantity zero (Invalid)
    Order zero_qty{61, OrderSide::Buy, OrderType::Market, 0, std::nullopt, OrderStatus::New, 0};
    auto res_zero = engine.submit_order(zero_qty);
    assert(res_zero.order.status == OrderStatus::Rejected);
    assert(res_zero.trades.empty());

    // 3. Limit order missing limit price (Invalid)
    Order missing_limit{62, OrderSide::Sell, OrderType::Limit, 10, std::nullopt, OrderStatus::New, 0};
    auto res_missing = engine.submit_order(missing_limit);
    assert(res_missing.order.status == OrderStatus::Rejected);
    assert(res_missing.trades.empty());

    // 4. Market order specifying limit price (Invalid)
    Order market_with_limit{63, OrderSide::Buy, OrderType::Market, 10, 50.0, OrderStatus::New, 0};
    auto res_market_lim = engine.submit_order(market_with_limit);
    assert(res_market_lim.order.status == OrderStatus::Rejected);
    assert(res_market_lim.trades.empty());

    // Book remains pristine
    assert(engine.get_order_book().empty());

    std::cout << "  -> PASSED: Invalid orders rejected and empty book handled safely.\n\n";
}

void test_limit_price_protection() {
    std::cout << "[Test 8] Testing limit price protection...\n";
    ExecutionEngine engine;

    // Resting Ask @ 105.00
    engine.submit_order(Order::create_limit(70, OrderSide::Sell, 50, 105.00));

    // Incoming Buy Limit @ 104.00 (below Ask)
    // Must NOT execute at 105.00!
    auto res1 = engine.submit_order(Order::create_limit(71, OrderSide::Buy, 50, 104.00));
    assert(res1.trades.empty());
    assert(res1.order.status == OrderStatus::New);
    assert(engine.get_order_book().has_bids() && engine.get_order_book().has_asks());

    // Incoming Sell Limit @ 103.00 (crosses resting Bid @ 104.00)
    // Execution price should be 104.00 (resting bid price), satisfying both seller (>= 103) and buyer (<= 104).
    auto res2 = engine.submit_order(Order::create_limit(72, OrderSide::Sell, 50, 103.00));
    assert(res2.trades.size() == 1);
    assert(res2.trades[0].price == 104.00);
    assert(res2.trades[0].buy_order_id == 71);
    assert(res2.trades[0].sell_order_id == 72);
    assert(res2.order.status == OrderStatus::Filled);

    std::cout << "  -> PASSED: Limit prices strictly protected on both aggressive and resting sides.\n\n";
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "   Running Execution Engine Unit Tests (Module 4)   \n";
    std::cout << "====================================================\n\n";

    test_matching_buy_sell();
    test_non_crossing_orders_remain_in_book();
    test_price_time_priority();
    test_partial_fills();
    test_multiple_trades_from_single_order();
    test_market_orders_insufficient_liquidity();
    test_invalid_orders_and_empty_book();
    test_limit_price_protection();

    std::cout << "====================================================\n";
    std::cout << "   ALL EXECUTION ENGINE TESTS PASSED SUCCESSFULLY!  \n";
    std::cout << "====================================================\n";

    return 0;
}
