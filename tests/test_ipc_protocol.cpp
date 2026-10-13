#include "../engine/IPCProtocol.hpp"
#include "../engine/Order.hpp"
#include "../engine/Portfolio.hpp"
#include "../engine/Trade.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// Helper to verify an exception is thrown
#define ASSERT_THROWS(expr, ExpectedException) \
    do { \
        bool caught = false; \
        try { \
            expr; \
        } catch (const ExpectedException&) { \
            caught = true; \
        } catch (...) { \
        } \
        assert(caught && "Expected exception was not thrown: " #expr); \
    } while (0)

using namespace hft::ipc;

void test_version_message() {
    std::cout << "[Test 1] Testing VERSION message serialization & parsing...\n";

    // 1. Valid serialization
    std::string serialized = serialize_version(1);
    assert(serialized == "VERSION\t1");

    // 2. Valid parsing
    VersionMessage parsed = parse_version(serialized);
    assert(parsed.version == 1);

    // 3. Serialization rejects version 0
    ASSERT_THROWS(serialize_version(0), std::invalid_argument);

    // 4. Parsing rejects extra fields
    ASSERT_THROWS(parse_version("VERSION\t1\textra"), std::invalid_argument);

    // 5. Parsing rejects missing fields
    ASSERT_THROWS(parse_version("VERSION"), std::invalid_argument);

    // 6. Parsing rejects non-numeric version
    ASSERT_THROWS(parse_version("VERSION\tabc"), std::invalid_argument);

    // 7. Parsing rejects version 0
    ASSERT_THROWS(parse_version("VERSION\t0"), std::invalid_argument);

    std::cout << "  -> PASSED: VERSION message valid & invalid cases verified.\n\n";
}

void test_tick_message() {
    std::cout << "[Test 2] Testing TICK message serialization & parsing...\n";

    // 1. Valid serialization
    TickMessage msg{1, 42, 105.75};
    std::string serialized = serialize_tick(msg);
    assert(serialized == "TICK\t1\t42\t105.750000");

    // 2. Valid parsing
    TickMessage parsed = parse_tick(serialized);
    assert(parsed.version == 1);
    assert(parsed.tick_number == 42);
    assert(std::abs(parsed.price - 105.75) < 1e-5);

    // 3. Rejects invalid price during serialization
    ASSERT_THROWS(serialize_tick(1, 0.0, 1), std::invalid_argument);
    ASSERT_THROWS(serialize_tick(1, -10.0, 1), std::invalid_argument);
    ASSERT_THROWS(serialize_tick(1, std::numeric_limits<double>::quiet_NaN(), 1), std::invalid_argument);
    ASSERT_THROWS(serialize_tick(1, std::numeric_limits<double>::infinity(), 1), std::invalid_argument);

    // 4. Rejects zero tick_number
    ASSERT_THROWS(serialize_tick(0, 100.0, 1), std::invalid_argument);

    // 5. Parsing rejects malformed fields
    ASSERT_THROWS(parse_tick("TICK\t1\t42"), std::invalid_argument); // missing price
    ASSERT_THROWS(parse_tick("TICK\t1\t42\t100.0\textra"), std::invalid_argument); // extra field
    ASSERT_THROWS(parse_tick("TICK\t1\t0\t100.0"), std::invalid_argument); // zero tick_number
    ASSERT_THROWS(parse_tick("TICK\t1\t42\t0.0"), std::invalid_argument); // zero price
    ASSERT_THROWS(parse_tick("TICK\t1\t42\t-5.0"), std::invalid_argument); // negative price
    ASSERT_THROWS(parse_tick("TICK\t1\t42\tnan"), std::invalid_argument); // NaN price
    ASSERT_THROWS(parse_tick("TICK\t1\t42\tinf"), std::invalid_argument); // Inf price
    ASSERT_THROWS(parse_tick("TICK\t1\tnot_a_num\t100.0"), std::invalid_argument); // invalid int

    std::cout << "  -> PASSED: TICK message valid & invalid cases verified.\n\n";
}

void test_trade_message() {
    std::cout << "[Test 3] Testing TRADE message serialization & parsing...\n";

    // 1. Valid serialization
    Trade trade{7, 101, 102, 99.5, 25};
    std::string serialized = serialize_trade(trade);
    assert(serialized == "TRADE\t1\t7\t101\t102\t99.500000\t25");

    // 2. Valid parsing
    TradeMessage parsed = parse_trade(serialized);
    assert(parsed.version == 1);
    assert(parsed.trade_id == 7);
    assert(parsed.buy_order_id == 101);
    assert(parsed.sell_order_id == 102);
    assert(std::abs(parsed.price - 99.5) < 1e-5);
    assert(parsed.quantity == 25);

    // 3. Serialization rejects invalid inputs
    ASSERT_THROWS(serialize_trade(Trade{0, 1, 2, 100.0, 10}), std::invalid_argument); // zero trade_id
    ASSERT_THROWS(serialize_trade(Trade{1, 0, 2, 100.0, 10}), std::invalid_argument); // zero buy_order_id
    ASSERT_THROWS(serialize_trade(Trade{1, 1, 0, 100.0, 10}), std::invalid_argument); // zero sell_order_id
    ASSERT_THROWS(serialize_trade(Trade{1, 1, 2, -10.0, 10}), std::invalid_argument); // negative price
    ASSERT_THROWS(serialize_trade(Trade{1, 1, 2, 100.0, 0}), std::invalid_argument); // zero qty

    // 4. Parsing rejects malformed lines
    ASSERT_THROWS(parse_trade("TRADE\t1\t7\t101\t102\t99.5"), std::invalid_argument); // missing qty
    ASSERT_THROWS(parse_trade("TRADE\t1\t7\t101\t102\t99.5\t25\textra"), std::invalid_argument); // extra
    ASSERT_THROWS(parse_trade("TRADE\t1\t7\t101\t102\t99.5\t0"), std::invalid_argument); // zero qty
    ASSERT_THROWS(parse_trade("TRADE\t1\t7\t101\t102\tnan\t25"), std::invalid_argument); // NaN price
    ASSERT_THROWS(parse_trade("TRADE\t1\t7\t101\t102\t-1.0\t25"), std::invalid_argument); // negative price

    std::cout << "  -> PASSED: TRADE message valid & invalid cases verified.\n\n";
}

void test_portfolio_message() {
    std::cout << "[Test 4] Testing PORTFOLIO message serialization & parsing...\n";

    // 1. Direct struct serialization
    PortfolioMessage msg{1, 9500.5, 50, 100.25, 12.5, -5.0, 7.5};
    std::string serialized = serialize_portfolio(msg);
    assert(serialized == "PORTFOLIO\t1\t9500.500000\t50\t100.250000\t12.500000\t-5.000000\t7.500000");

    // 2. Direct struct parsing
    PortfolioMessage parsed = parse_portfolio(serialized);
    assert(parsed.version == 1);
    assert(std::abs(parsed.cash - 9500.5) < 1e-5);
    assert(parsed.position_quantity == 50);
    assert(std::abs(parsed.average_entry_price - 100.25) < 1e-5);
    assert(std::abs(parsed.realized_pnl - 12.5) < 1e-5);
    assert(std::abs(parsed.unrealized_pnl - (-5.0)) < 1e-5);
    assert(std::abs(parsed.total_pnl - 7.5) < 1e-5);

    // 3. Engine Portfolio object serialization
    Portfolio p(10000.0);
    Trade buy_trade{1, 1, 2, 100.0, 20};
    p.apply_trade(buy_trade, OrderSide::Buy);
    std::string p_serialized = serialize_portfolio(p, 105.0);
    PortfolioMessage p_parsed = parse_portfolio(p_serialized);
    assert(p_parsed.cash == 8000.0);
    assert(p_parsed.position_quantity == 20);
    assert(p_parsed.average_entry_price == 100.0);
    assert(p_parsed.realized_pnl == 0.0);
    assert(p_parsed.unrealized_pnl == 100.0); // 20 * (105 - 100)
    assert(p_parsed.total_pnl == 100.0);

    // 4. Rejection of invalid inputs
    ASSERT_THROWS(serialize_portfolio(p, 0.0), std::invalid_argument); // non-positive mark price
    ASSERT_THROWS(serialize_portfolio(p, -50.0), std::invalid_argument); // negative mark price
    ASSERT_THROWS(serialize_portfolio(p, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);

    // 5. Parsing rejects wrong field counts & malformed fields
    ASSERT_THROWS(parse_portfolio("PORTFOLIO\t1\t10000.0\t0\t0.0\t0.0\t0.0"), std::invalid_argument); // 7 fields
    ASSERT_THROWS(parse_portfolio("PORTFOLIO\t1\t10000.0\t0\t0.0\t0.0\t0.0\t0.0\textra"), std::invalid_argument); // 9 fields
    ASSERT_THROWS(parse_portfolio("PORTFOLIO\t1\tnan\t0\t0.0\t0.0\t0.0\t0.0"), std::invalid_argument); // NaN cash
    ASSERT_THROWS(parse_portfolio("PORTFOLIO\t1\t10000.0\t0\t-1.0\t0.0\t0.0\t0.0"), std::invalid_argument); // negative avg price

    std::cout << "  -> PASSED: PORTFOLIO message valid & invalid cases verified.\n\n";
}

void test_end_message() {
    std::cout << "[Test 5] Testing END message serialization & parsing...\n";

    // 1. Valid serialization
    std::string serialized = serialize_end(1);
    assert(serialized == "END\t1");

    // 2. Valid parsing
    EndMessage parsed = parse_end(serialized);
    assert(parsed.version == 1);

    // 3. Serialization rejects version 0
    ASSERT_THROWS(serialize_end(0), std::invalid_argument);

    // 4. Parsing rejects extra fields
    ASSERT_THROWS(parse_end("END\t1\textra"), std::invalid_argument);

    // 5. Parsing rejects non-numeric version
    ASSERT_THROWS(parse_end("END\tbad"), std::invalid_argument);

    std::cout << "  -> PASSED: END message valid & invalid cases verified.\n\n";
}

void test_peek_message_type() {
    std::cout << "[Test 6] Testing peek_message_type and string conversions...\n";

    assert(peek_message_type("VERSION\t1") == MessageType::Version);
    assert(peek_message_type("TICK\t1\t10\t100.5") == MessageType::Tick);
    assert(peek_message_type("TRADE\t1\t1\t2\t3\t100.0\t5") == MessageType::Trade);
    assert(peek_message_type("PORTFOLIO\t1\t100\t0\t0\t0\t0\t0") == MessageType::Portfolio);
    assert(peek_message_type("END\t1") == MessageType::End);
    assert(peek_message_type("UNKNOWN_TYPE\t1") == MessageType::Unknown);
    assert(peek_message_type("") == MessageType::Unknown);

    assert(to_string(MessageType::Version) == "VERSION");
    assert(to_string(MessageType::Tick) == "TICK");
    assert(to_string(MessageType::Trade) == "TRADE");
    assert(to_string(MessageType::Portfolio) == "PORTFOLIO");
    assert(to_string(MessageType::End) == "END");
    assert(to_string(MessageType::Unknown) == "UNKNOWN");

    std::cout << "  -> PASSED: peek_message_type works accurately.\n\n";
}

void test_protocol_writer() {
    std::cout << "[Test 7] Testing ProtocolWriter streaming to std::ostringstream...\n";

    std::ostringstream oss;
    ProtocolWriter writer(oss);

    writer.write_version(1);
    writer.write_tick(1, 100.5);
    writer.write_end(1);

    std::string expected = "VERSION\t1\nTICK\t1\t1\t100.500000\nEND\t1\n";
    assert(oss.str() == expected);

    std::cout << "  -> PASSED: ProtocolWriter produces clean newline-delimited stream.\n\n";
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "   Running C++ IPC Protocol Unit Tests (Module 7)   \n";
    std::cout << "====================================================\n\n";

    test_version_message();
    test_tick_message();
    test_trade_message();
    test_portfolio_message();
    test_end_message();
    test_peek_message_type();
    test_protocol_writer();

    std::cout << "====================================================\n";
    std::cout << "   ALL IPC PROTOCOL TESTS PASSED SUCCESSFULLY!      \n";
    std::cout << "====================================================\n";

    return 0;
}
