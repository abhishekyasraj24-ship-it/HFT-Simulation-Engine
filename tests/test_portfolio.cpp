#include "../engine/Portfolio.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Trade make_trade(TradeId id, std::uint32_t qty, double price) {
    Trade t;
    t.trade_id       = id;
    t.buy_order_id   = id * 10;
    t.sell_order_id  = id * 10 + 1;
    t.quantity       = qty;
    t.price          = price;
    return t;
}

static bool approx_equal(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) < eps;
}

// ---------------------------------------------------------------------------
// Test 1: Initial state
// ---------------------------------------------------------------------------
void test_initial_state() {
    std::cout << "[Test 1] Verifying initial portfolio state...\n";
    Portfolio p(5000.0);
    assert(approx_equal(p.get_starting_cash(), 5000.0));
    assert(approx_equal(p.get_cash(), 5000.0));
    assert(p.get_position_quantity() == 0);
    assert(approx_equal(p.get_average_entry_price(), 0.0));
    assert(approx_equal(p.get_realized_pnl(), 0.0));
    assert(approx_equal(p.get_unrealized_pnl(100.0), 0.0));
    assert(approx_equal(p.get_total_pnl(100.0), 0.0));
    assert(approx_equal(p.get_total_equity(100.0), 5000.0));
    assert(p.get_last_error().empty());
    std::cout << "  -> PASSED: Default portfolio initializes with correct state.\n\n";
}

// ---------------------------------------------------------------------------
// Test 2: Single buy
// ---------------------------------------------------------------------------
void test_single_buy() {
    std::cout << "[Test 2] Testing single buy trade...\n";
    Portfolio p(10000.0);
    bool ok = p.process_trade(make_trade(1, 10, 100.0), OrderSide::Buy);
    assert(ok);
    assert(p.get_last_error().empty());
    assert(approx_equal(p.get_cash(), 9000.0));
    assert(p.get_position_quantity() == 10);
    assert(approx_equal(p.get_average_entry_price(), 100.0));
    assert(approx_equal(p.get_realized_pnl(), 0.0));
    assert(approx_equal(p.get_unrealized_pnl(105.0), 50.0));
    assert(approx_equal(p.get_total_equity(105.0), 10050.0));
    std::cout << "  -> PASSED: Single buy correctly updates cash, position, and avg entry price.\n\n";
}

// ---------------------------------------------------------------------------
// Test 3: Single sell + realized P&L
// ---------------------------------------------------------------------------
void test_single_sell() {
    std::cout << "[Test 3] Testing single sell trade (realized P&L)...\n";
    Portfolio p(10000.0);
    p.process_trade(make_trade(1, 10, 100.0), OrderSide::Buy);
    bool ok = p.process_trade(make_trade(2, 5, 110.0), OrderSide::Sell);
    assert(ok);
    assert(approx_equal(p.get_cash(), 9550.0));
    assert(p.get_position_quantity() == 5);
    assert(approx_equal(p.get_average_entry_price(), 100.0));
    assert(approx_equal(p.get_realized_pnl(), 50.0));
    assert(approx_equal(p.get_unrealized_pnl(108.0), 40.0));
    assert(approx_equal(p.get_total_pnl(108.0), 90.0));
    std::cout << "  -> PASSED: Sell correctly realises P&L and reduces position.\n\n";
}

// ---------------------------------------------------------------------------
// Test 4: Full close resets avg entry price
// ---------------------------------------------------------------------------
void test_full_close_resets_avg_price() {
    std::cout << "[Test 4] Testing full position close resets avg entry price...\n";
    Portfolio p(10000.0);
    p.process_trade(make_trade(1, 20, 50.0), OrderSide::Buy);
    bool ok = p.process_trade(make_trade(2, 20, 60.0), OrderSide::Sell);
    assert(ok);
    assert(p.get_position_quantity() == 0);
    assert(approx_equal(p.get_average_entry_price(), 0.0));
    assert(approx_equal(p.get_realized_pnl(), 200.0));
    assert(approx_equal(p.get_unrealized_pnl(999.0), 0.0));
    assert(approx_equal(p.get_total_equity(999.0), 10200.0));
    std::cout << "  -> PASSED: Full close resets avg entry price; realized P&L correct.\n\n";
}

// ---------------------------------------------------------------------------
// Test 5: VWAP average entry price
// ---------------------------------------------------------------------------
void test_vwap_average_entry_price() {
    std::cout << "[Test 5] Testing VWAP average entry price across multiple buys...\n";
    Portfolio p(50000.0);
    p.process_trade(make_trade(1, 10, 100.0), OrderSide::Buy);
    p.process_trade(make_trade(2, 20, 110.0), OrderSide::Buy);
    assert(p.get_position_quantity() == 30);
    double expected_avg = (10.0 * 100.0 + 20.0 * 110.0) / 30.0;
    assert(approx_equal(p.get_average_entry_price(), expected_avg));
    assert(approx_equal(p.get_cash(), 46800.0));
    std::cout << "  -> PASSED: VWAP correctly calculated after two buys at different prices.\n\n";
}

// ---------------------------------------------------------------------------
// Test 6: Realized P&L uses VWAP cost basis
// ---------------------------------------------------------------------------
void test_realized_pnl_vwap_basis() {
    std::cout << "[Test 6] Testing realized P&L uses VWAP cost basis...\n";
    Portfolio p(50000.0);
    p.process_trade(make_trade(1, 10, 100.0), OrderSide::Buy);
    p.process_trade(make_trade(2, 10, 120.0), OrderSide::Buy);  // avg = 110
    bool ok = p.process_trade(make_trade(3, 10, 115.0), OrderSide::Sell);
    assert(ok);
    assert(approx_equal(p.get_realized_pnl(), 50.0));  // 10 * (115 - 110)
    assert(p.get_position_quantity() == 10);
    assert(approx_equal(p.get_average_entry_price(), 110.0));
    std::cout << "  -> PASSED: Realized P&L uses VWAP cost basis correctly.\n\n";
}

// ---------------------------------------------------------------------------
// Test 7: Insufficient cash rejects buy
// ---------------------------------------------------------------------------
void test_insufficient_cash_rejects_buy() {
    std::cout << "[Test 7] Testing insufficient cash rejection for buy orders...\n";
    Portfolio p(500.0);
    Trade t = make_trade(1, 10, 100.0);  // costs $1000 > $500
    bool ok = p.process_trade(t, OrderSide::Buy);
    assert(!ok);
    assert(!p.get_last_error().empty());
    assert(approx_equal(p.get_cash(), 500.0));
    assert(p.get_position_quantity() == 0);
    bool threw = false;
    try {
        Portfolio p2(500.0);
        p2.apply_trade(t, OrderSide::Buy);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    std::cout << "  -> PASSED: Insufficient cash correctly rejects buy and throws via apply_trade.\n\n";
}

// ---------------------------------------------------------------------------
// Test 8: Oversell rejects
// ---------------------------------------------------------------------------
void test_oversell_rejects() {
    std::cout << "[Test 8] Testing rejection of sell that exceeds current position...\n";
    Portfolio p(10000.0);
    p.process_trade(make_trade(1, 5, 100.0), OrderSide::Buy);
    bool ok = p.process_trade(make_trade(2, 10, 105.0), OrderSide::Sell);
    assert(!ok);
    assert(!p.get_last_error().empty());
    assert(p.get_position_quantity() == 5);
    assert(approx_equal(p.get_cash(), 9500.0));
    std::cout << "  -> PASSED: Oversell correctly rejected; portfolio state unchanged.\n\n";
}

// ---------------------------------------------------------------------------
// Test 9: Invalid trade inputs
// ---------------------------------------------------------------------------
void test_invalid_trade_inputs() {
    std::cout << "[Test 9] Testing rejection of invalid trade inputs...\n";
    Portfolio p(10000.0);
    assert(!p.process_trade(make_trade(1, 0, 100.0), OrderSide::Buy));    // zero qty
    assert(!p.process_trade(make_trade(2, 10, 0.0), OrderSide::Buy));     // zero price
    assert(!p.process_trade(make_trade(3, 10, -50.0), OrderSide::Buy));   // negative price
    assert(approx_equal(p.get_cash(), 10000.0));
    assert(p.get_position_quantity() == 0);
    std::cout << "  -> PASSED: Invalid inputs (zero qty, non-positive price) correctly rejected.\n\n";
}

// ---------------------------------------------------------------------------
// Test 10: Trade ID deduplication
// ---------------------------------------------------------------------------
void test_trade_id_deduplication() {
    std::cout << "[Test 10] Testing trade ID deduplication...\n";
    Portfolio p(10000.0);
    Trade t = make_trade(42, 5, 100.0);
    assert(p.process_trade(t, OrderSide::Buy));
    assert(p.get_position_quantity() == 5);
    assert(p.has_processed_trade(42));
    // Duplicate must be rejected
    assert(!p.process_trade(t, OrderSide::Buy));
    assert(p.get_position_quantity() == 5);
    // trade_id == 0 is exempt from deduplication
    Trade t0 = make_trade(0, 5, 100.0);
    assert(p.process_trade(t0, OrderSide::Buy));
    assert(p.process_trade(t0, OrderSide::Buy));  // second time also OK
    std::cout << "  -> PASSED: Duplicate trade IDs rejected; trade_id=0 exempt from deduplication.\n\n";
}

// ---------------------------------------------------------------------------
// Test 11: Invalid starting cash throws
// ---------------------------------------------------------------------------
void test_invalid_starting_cash() {
    std::cout << "[Test 11] Testing invalid starting cash throws...\n";
    bool threw = false;
    try {
        Portfolio p(-1.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    std::cout << "  -> PASSED: Negative starting cash throws std::invalid_argument.\n\n";
}

// ---------------------------------------------------------------------------
// Test 12: reset() restores portfolio
// ---------------------------------------------------------------------------
void test_reset() {
    std::cout << "[Test 12] Testing portfolio reset...\n";
    Portfolio p(10000.0);
    p.process_trade(make_trade(1, 10, 100.0), OrderSide::Buy);
    p.process_trade(make_trade(2,  5, 110.0), OrderSide::Sell);
    p.reset(20000.0);
    assert(approx_equal(p.get_starting_cash(), 20000.0));
    assert(approx_equal(p.get_cash(), 20000.0));
    assert(p.get_position_quantity() == 0);
    assert(approx_equal(p.get_average_entry_price(), 0.0));
    assert(approx_equal(p.get_realized_pnl(), 0.0));
    assert(!p.has_processed_trade(1));
    std::cout << "  -> PASSED: reset() restores portfolio to clean state with new starting cash.\n\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "====================================================\n";
    std::cout << "   Running Portfolio Unit Tests (Module 5)          \n";
    std::cout << "====================================================\n\n";

    test_initial_state();
    test_single_buy();
    test_single_sell();
    test_full_close_resets_avg_price();
    test_vwap_average_entry_price();
    test_realized_pnl_vwap_basis();
    test_insufficient_cash_rejects_buy();
    test_oversell_rejects();
    test_invalid_trade_inputs();
    test_trade_id_deduplication();
    test_invalid_starting_cash();
    test_reset();

    std::cout << "====================================================\n";
    std::cout << "   ALL PORTFOLIO TESTS PASSED SUCCESSFULLY!         \n";
    std::cout << "====================================================\n";

    return 0;
}
