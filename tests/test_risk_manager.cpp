#include "../engine/Portfolio.hpp"
#include "../engine/RiskManager.hpp"
#include "../engine/Order.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Build a Portfolio that already holds a position by simulating prior buys.
 * Uses trade_id == 0 so deduplication never blocks repeated builds.
 */
static Portfolio make_portfolio_with_position(double cash,
                                              std::uint32_t qty,
                                              double avg_price) {
    // Starting cash must cover the position cost plus remain the desired cash.
    double starting_cash = cash + static_cast<double>(qty) * avg_price;
    Portfolio p(starting_cash);
    if (qty > 0) {
        Trade t;
        t.trade_id      = 0;  // exempt from deduplication
        t.buy_order_id  = 99;
        t.sell_order_id = 100;
        t.quantity      = qty;
        t.price         = avg_price;
        bool ok = p.process_trade(t, OrderSide::Buy);
        assert(ok && "make_portfolio_with_position: trade rejected");
    }
    return p;
}

/** Convenience: unlimited limits (all zeros → no enforcement). */
static RiskLimits unlimited() {
    return RiskLimits{0, 0.0, 0, 0.0};
}

// ---------------------------------------------------------------------------
// Test 1: Valid limit buy order approved on unlimited limits
// ---------------------------------------------------------------------------
void test_valid_order_approved() {
    std::cout << "[Test 1] Valid order is approved with unlimited limits...\n";

    RiskManager rm(unlimited());
    Portfolio p(50000.0);  // flat, no position
    Order o = Order::create_limit(1, OrderSide::Buy, 100, 100.0);

    auto res = rm.check_order(o, p, 100.0);
    assert(res.approved);
    assert(res.rejection_reason.empty());

    std::cout << "  -> PASSED: Valid buy limit order approved.\n\n";
}

// ---------------------------------------------------------------------------
// Test 2: Invalid order structure is rejected (zero quantity)
// ---------------------------------------------------------------------------
void test_invalid_order_structure_rejected() {
    std::cout << "[Test 2] Structurally invalid order (zero qty) is rejected...\n";

    RiskManager rm(unlimited());
    Portfolio p(50000.0);

    // Build an invalid order directly (bypass factory validators)
    Order bad_order;
    bad_order.id       = 2;
    bad_order.side     = OrderSide::Buy;
    bad_order.type     = OrderType::Market;
    bad_order.quantity = 0;  // invalid

    auto res = rm.check_order(bad_order, p, 100.0);
    assert(!res.approved);
    assert(!res.rejection_reason.empty());

    std::cout << "  -> PASSED: Zero-quantity order rejected by structural check.\n\n";
}

// ---------------------------------------------------------------------------
// Test 3: Limit order missing price is rejected
// ---------------------------------------------------------------------------
void test_limit_order_missing_price_rejected() {
    std::cout << "[Test 3] Limit order without limit_price is rejected...\n";

    RiskManager rm(unlimited());
    Portfolio p(50000.0);

    Order bad_order;
    bad_order.id        = 3;
    bad_order.side      = OrderSide::Buy;
    bad_order.type      = OrderType::Limit;
    bad_order.quantity  = 10;
    bad_order.limit_price = std::nullopt;  // missing → invalid

    auto res = rm.check_order(bad_order, p, 100.0);
    assert(!res.approved);

    std::cout << "  -> PASSED: Limit order without price correctly rejected.\n\n";
}

// ---------------------------------------------------------------------------
// Test 4: Invalid reference market price is rejected
// ---------------------------------------------------------------------------
void test_invalid_ref_price_rejected() {
    std::cout << "[Test 4] Invalid reference market prices are rejected...\n";

    RiskManager rm(unlimited());
    Portfolio p(50000.0);
    Order o = Order::create_market(4, OrderSide::Buy, 10);

    auto res_zero = rm.check_order(o, p, 0.0);
    assert(!res_zero.approved);

    auto res_neg = rm.check_order(o, p, -50.0);
    assert(!res_neg.approved);

    auto res_nan = rm.check_order(o, p, std::numeric_limits<double>::quiet_NaN());
    assert(!res_nan.approved);

    auto res_inf = rm.check_order(o, p, std::numeric_limits<double>::infinity());
    assert(!res_inf.approved);

    std::cout << "  -> PASSED: Zero, negative, NaN, and infinite ref prices all rejected.\n\n";
}

// ---------------------------------------------------------------------------
// Test 5: Per-order quantity limit enforced
// ---------------------------------------------------------------------------
void test_max_order_quantity_enforced() {
    std::cout << "[Test 5] Per-order quantity limit is enforced...\n";

    RiskLimits lim{50, 0.0, 0, 0.0};  // max_order_quantity = 50
    RiskManager rm(lim);
    Portfolio p(100000.0);

    // Exactly at the limit → approved
    Order at_limit = Order::create_market(5, OrderSide::Buy, 50);
    auto res_ok = rm.check_order(at_limit, p, 100.0);
    assert(res_ok.approved);

    // One over the limit → rejected
    Order over_limit = Order::create_market(6, OrderSide::Buy, 51);
    auto res_bad = rm.check_order(over_limit, p, 100.0);
    assert(!res_bad.approved);
    assert(!res_bad.rejection_reason.empty());

    std::cout << "  -> PASSED: Exactly at limit approved; one over rejected.\n\n";
}

// ---------------------------------------------------------------------------
// Test 6: Per-order notional limit enforced (limit order uses limit_price)
// ---------------------------------------------------------------------------
void test_max_order_notional_enforced_limit_order() {
    std::cout << "[Test 6] Per-order notional limit enforced on limit order...\n";

    // notional limit = $5000
    RiskLimits lim{0, 5000.0, 0, 0.0};
    RiskManager rm(lim);
    Portfolio p(100000.0);

    // 50 units @ $100 = $5000 → exactly at limit → approved
    Order at_limit = Order::create_limit(7, OrderSide::Buy, 50, 100.0);
    auto res_ok = rm.check_order(at_limit, p, 99.0);  // ref price irrelevant for notional
    assert(res_ok.approved);

    // 51 units @ $100 = $5100 → over limit → rejected
    Order over_limit = Order::create_limit(8, OrderSide::Buy, 51, 100.0);
    auto res_bad = rm.check_order(over_limit, p, 99.0);
    assert(!res_bad.approved);

    std::cout << "  -> PASSED: Limit-order notional uses limit_price; boundary correct.\n\n";
}

// ---------------------------------------------------------------------------
// Test 7: Per-order notional limit enforced (market order uses ref_price)
// ---------------------------------------------------------------------------
void test_max_order_notional_enforced_market_order() {
    std::cout << "[Test 7] Per-order notional limit enforced on market order...\n";

    RiskLimits lim{0, 5000.0, 0, 0.0};
    RiskManager rm(lim);
    Portfolio p(100000.0);

    // 50 units @ ref $100 = $5000 → exactly at limit → approved
    Order at_limit = Order::create_market(9, OrderSide::Buy, 50);
    auto res_ok = rm.check_order(at_limit, p, 100.0);
    assert(res_ok.approved);

    // 50 units @ ref $101 = $5050 → over limit → rejected
    auto res_bad = rm.check_order(at_limit, p, 101.0);
    assert(!res_bad.approved);

    std::cout << "  -> PASSED: Market-order notional uses ref_price; boundary correct.\n\n";
}

// ---------------------------------------------------------------------------
// Test 8: Max position quantity enforced on buy
// ---------------------------------------------------------------------------
void test_max_position_quantity_enforced_buy() {
    std::cout << "[Test 8] Max position quantity enforced for buy orders...\n";

    // max position = 100 units
    RiskLimits lim{0, 0.0, 100, 0.0};
    RiskManager rm(lim);

    // Portfolio already holds 80 units
    Portfolio p = make_portfolio_with_position(10000.0, 80, 100.0);
    assert(p.get_position_quantity() == 80);

    // Buy 20 → projected = 100 → exactly at limit → approved
    Order at_limit = Order::create_market(10, OrderSide::Buy, 20);
    auto res_ok = rm.check_order(at_limit, p, 100.0);
    assert(res_ok.approved);

    // Buy 21 → projected = 101 → over limit → rejected
    Order over_limit = Order::create_market(11, OrderSide::Buy, 21);
    auto res_bad = rm.check_order(over_limit, p, 100.0);
    assert(!res_bad.approved);
    assert(!res_bad.rejection_reason.empty());

    std::cout << "  -> PASSED: Position limit boundary (80 current + limit 100) correct.\n\n";
}

// ---------------------------------------------------------------------------
// Test 9: Overselling (selling more than held) is rejected
// ---------------------------------------------------------------------------
void test_oversell_rejected() {
    std::cout << "[Test 9] Oversell (sell > current position) is rejected...\n";

    RiskManager rm(unlimited());
    Portfolio p = make_portfolio_with_position(5000.0, 10, 100.0);
    assert(p.get_position_quantity() == 10);

    // Sell exactly 10 → approved
    Order ok_sell = Order::create_market(12, OrderSide::Sell, 10);
    auto res_ok = rm.check_order(ok_sell, p, 100.0);
    assert(res_ok.approved);

    // Sell 11 → rejected
    Order bad_sell = Order::create_market(13, OrderSide::Sell, 11);
    auto res_bad = rm.check_order(bad_sell, p, 100.0);
    assert(!res_bad.approved);
    assert(!res_bad.rejection_reason.empty());

    // Sell on flat portfolio → rejected
    Portfolio flat(50000.0);
    Order sell_flat = Order::create_market(14, OrderSide::Sell, 1);
    auto res_flat = rm.check_order(sell_flat, flat, 100.0);
    assert(!res_flat.approved);

    std::cout << "  -> PASSED: Oversell and sell-on-flat-portfolio correctly rejected.\n\n";
}

// ---------------------------------------------------------------------------
// Test 10: Gross exposure limit enforced on buy
// ---------------------------------------------------------------------------
void test_gross_exposure_enforced_buy() {
    std::cout << "[Test 10] Gross exposure limit enforced on buy orders...\n";

    // max_gross_exposure = $10000; ref_price = $100
    // → max allowed position = 100 units
    RiskLimits lim{0, 0.0, 0, 10000.0};
    RiskManager rm(lim);
    Portfolio p = make_portfolio_with_position(10000.0, 80, 100.0);

    // Buy 20 → projected 100 units * $100 = $10000 → exactly at limit → approved
    Order at_limit = Order::create_market(15, OrderSide::Buy, 20);
    auto res_ok = rm.check_order(at_limit, p, 100.0);
    assert(res_ok.approved);

    // Buy 21 → projected 101 units * $100 = $10100 → over limit → rejected
    Order over_limit = Order::create_market(16, OrderSide::Buy, 21);
    auto res_bad = rm.check_order(over_limit, p, 100.0);
    assert(!res_bad.approved);

    std::cout << "  -> PASSED: Gross exposure boundary (80 held, limit $10000 @ $100) correct.\n\n";
}

// ---------------------------------------------------------------------------
// Test 11: Gross exposure limit: sell always reduces exposure (approves)
// ---------------------------------------------------------------------------
void test_gross_exposure_sell_reduces() {
    std::cout << "[Test 11] Sells always reduce gross exposure (should be approved)...\n";

    // Tight exposure limit; portfolio already exceeds it — sells should pass
    // because they move us closer to compliance.
    RiskLimits lim{0, 0.0, 0, 5000.0};  // limit $5000 @ $100 = 50 units
    RiskManager rm(lim);

    // Portfolio holds 60 units (exposure $6000 — already over limit).
    // A sell of 10 → projected 50 units * $100 = $5000 → exactly at limit → approved.
    Portfolio p = make_portfolio_with_position(10000.0, 60, 100.0);

    Order sell_10 = Order::create_market(17, OrderSide::Sell, 10);
    auto res = rm.check_order(sell_10, p, 100.0);
    assert(res.approved);

    std::cout << "  -> PASSED: Sell that brings exposure to exactly the limit is approved.\n\n";
}

// ---------------------------------------------------------------------------
// Test 12: Risk checks do NOT modify portfolio state
// ---------------------------------------------------------------------------
void test_risk_check_does_not_modify_portfolio() {
    std::cout << "[Test 12] Risk checks do not modify portfolio state...\n";

    RiskManager rm(unlimited());
    Portfolio p(10000.0);

    const double   cash_before = p.get_cash();
    const uint32_t pos_before  = p.get_position_quantity();

    // Approved check
    Order o1 = Order::create_limit(18, OrderSide::Buy, 10, 100.0);
    rm.check_order(o1, p, 100.0);

    // Rejected check (oversell on flat portfolio)
    Order o2 = Order::create_market(19, OrderSide::Sell, 5);
    rm.check_order(o2, p, 100.0);

    assert(p.get_cash()              == cash_before);
    assert(p.get_position_quantity() == pos_before);
    assert(p.get_realized_pnl()      == 0.0);

    std::cout << "  -> PASSED: Portfolio state unchanged after approved and rejected checks.\n\n";
}

// ---------------------------------------------------------------------------
// Test 13: Invalid RiskLimits configuration throws on construction
// ---------------------------------------------------------------------------
void test_invalid_risk_limits_throw() {
    std::cout << "[Test 13] Invalid RiskLimits configurations throw on construction...\n";

    // Negative notional
    bool threw = false;
    try {
        RiskLimits bad{0, -1.0, 0, 0.0};
        RiskManager rm(bad);
    } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    // Negative exposure
    threw = false;
    try {
        RiskLimits bad{0, 0.0, 0, -100.0};
        RiskManager rm(bad);
    } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    // max_order_quantity > max_position_quantity
    threw = false;
    try {
        RiskLimits bad{200, 0.0, 100, 0.0};  // order qty 200 > position 100
        RiskManager rm(bad);
    } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    // Infinite notional → invalid
    threw = false;
    try {
        RiskLimits bad{0, std::numeric_limits<double>::infinity(), 0, 0.0};
        RiskManager rm(bad);
    } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    std::cout << "  -> PASSED: All invalid limit configurations correctly throw on construction.\n\n";
}

// ---------------------------------------------------------------------------
// Test 14: All limits together — exact boundary passes, one over fails
// ---------------------------------------------------------------------------
void test_combined_limits_boundary() {
    std::cout << "[Test 14] Combined limits: exact boundary passes, one-over fails...\n";

    // Tight limits: max 50 qty/order, $5000 notional, 100 max position, $10000 exposure
    RiskLimits lim{50, 5000.0, 100, 10000.0};
    RiskManager rm(lim);

    // Start flat — buy 50 @ $100 = $5000 notional, projected pos 50, exposure $5000
    Portfolio p(100000.0);
    Order at_boundary = Order::create_limit(20, OrderSide::Buy, 50, 100.0);
    auto res_ok = rm.check_order(at_boundary, p, 100.0);
    assert(res_ok.approved);

    // Same order but 51 units → violates max_order_quantity (50)
    Order over_qty = Order::create_limit(21, OrderSide::Buy, 51, 100.0);
    // Note: max_order_quantity < max_position_quantity here would be violated,
    // but 51 > 50 so it fails qty check first.
    // To test this independently, use limits that allow 51 units but cap notional:
    RiskLimits lim2{0, 5000.0, 0, 0.0};
    RiskManager rm2(lim2);
    // 51 units @ $100 = $5100 > $5000 notional cap
    auto res_notional = rm2.check_order(over_qty, p, 100.0);
    assert(!res_notional.approved);

    std::cout << "  -> PASSED: Combined boundary checks work correctly.\n\n";
}

// ---------------------------------------------------------------------------
// Test 15: Market vs Limit order notional calculation difference
// ---------------------------------------------------------------------------
void test_market_vs_limit_notional_price_policy() {
    std::cout << "[Test 15] Notional uses limit_price for limit orders, ref_price for market...\n";

    // Notional cap = $5000; ref_price = $200
    // 25 units * $200 = $5000 → market order exactly at limit
    // 25 units * $100 (limit_price) = $2500 → limit order well under limit
    RiskLimits lim{0, 5000.0, 0, 0.0};
    RiskManager rm(lim);
    Portfolio p(100000.0);

    // Market order: 25 * $200 ref = $5000 → approved (at boundary)
    Order mkt = Order::create_market(22, OrderSide::Buy, 25);
    assert(rm.check_order(mkt, p, 200.0).approved);

    // Market order: 26 * $200 ref = $5200 → rejected
    Order mkt_over = Order::create_market(23, OrderSide::Buy, 26);
    assert(!rm.check_order(mkt_over, p, 200.0).approved);

    // Limit order: 25 @ $100 limit_price = $2500 → approved even with ref = $200
    Order lmt = Order::create_limit(24, OrderSide::Buy, 25, 100.0);
    assert(rm.check_order(lmt, p, 200.0).approved);

    // Limit order: 51 @ $100 limit_price = $5100 → rejected
    Order lmt_over = Order::create_limit(25, OrderSide::Buy, 51, 100.0);
    assert(!rm.check_order(lmt_over, p, 200.0).approved);

    std::cout << "  -> PASSED: Limit and market orders use correct price for notional.\n\n";
}

// ---------------------------------------------------------------------------
// Test 16: Sell limit order with valid limits approved
// ---------------------------------------------------------------------------
void test_sell_limit_order_approved() {
    std::cout << "[Test 16] Valid sell limit order approved...\n";

    RiskLimits lim{50, 5000.0, 100, 0.0};
    RiskManager rm(lim);
    Portfolio p = make_portfolio_with_position(5000.0, 30, 100.0);

    // Sell 30 @ $105 → qty=30 ≤ 50, notional=30*105=$3150 ≤ $5000, position OK
    Order sell = Order::create_limit(26, OrderSide::Sell, 30, 105.0);
    auto res = rm.check_order(sell, p, 100.0);
    assert(res.approved);

    std::cout << "  -> PASSED: Sell limit order within all limits approved.\n\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "====================================================\n";
    std::cout << "   Running Risk Manager Unit Tests (Module 6)       \n";
    std::cout << "====================================================\n\n";

    test_valid_order_approved();
    test_invalid_order_structure_rejected();
    test_limit_order_missing_price_rejected();
    test_invalid_ref_price_rejected();
    test_max_order_quantity_enforced();
    test_max_order_notional_enforced_limit_order();
    test_max_order_notional_enforced_market_order();
    test_max_position_quantity_enforced_buy();
    test_oversell_rejected();
    test_gross_exposure_enforced_buy();
    test_gross_exposure_sell_reduces();
    test_risk_check_does_not_modify_portfolio();
    test_invalid_risk_limits_throw();
    test_combined_limits_boundary();
    test_market_vs_limit_notional_price_policy();
    test_sell_limit_order_approved();

    std::cout << "====================================================\n";
    std::cout << "   ALL RISK MANAGER TESTS PASSED SUCCESSFULLY!      \n";
    std::cout << "====================================================\n";

    return 0;
}
