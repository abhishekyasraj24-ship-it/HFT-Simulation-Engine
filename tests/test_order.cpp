#include "../engine/Order.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

void test_valid_buy_market_order() {
    std::cout << "[Test 1] Testing valid Buy Market order...\n";
    Order order{101, OrderSide::Buy, OrderType::Market, 150, std::nullopt, OrderStatus::New};

    assert(order.id == 101);
    assert(order.side == OrderSide::Buy);
    assert(order.type == OrderType::Market);
    assert(order.quantity == 150);
    assert(!order.limit_price.has_value());
    assert(order.status == OrderStatus::New);

    assert(order.is_valid());
    assert(!order.get_validation_error().has_value());
    order.validate(); // Should not throw

    // Also test factory helper
    Order factory_order = Order::create_market(102, OrderSide::Buy, 75);
    assert(factory_order.is_valid());
    assert(factory_order.quantity == 75);

    std::cout << "  - Order summary: " << order.to_string_summary() << "\n";
    std::cout << "  -> PASSED: Valid Buy Market order created and verified.\n\n";
}

void test_valid_sell_limit_order() {
    std::cout << "[Test 2] Testing valid Sell Limit order...\n";
    Order order{201, OrderSide::Sell, OrderType::Limit, 50, 105.50, OrderStatus::New};

    assert(order.id == 201);
    assert(order.side == OrderSide::Sell);
    assert(order.type == OrderType::Limit);
    assert(order.quantity == 50);
    assert(order.limit_price.has_value());
    assert(order.limit_price.value() == 105.50);
    assert(order.status == OrderStatus::New);

    assert(order.is_valid());
    order.validate(); // Should not throw

    // Also test factory helper
    Order factory_order = Order::create_limit(202, OrderSide::Sell, 25, 110.00);
    assert(factory_order.is_valid());
    assert(factory_order.limit_price.value() == 110.00);

    std::cout << "  - Order summary: " << order.to_string_summary() << "\n";
    std::cout << "  -> PASSED: Valid Sell Limit order created and verified.\n\n";
}

void test_zero_quantity() {
    std::cout << "[Test 3] Testing zero quantity rejection...\n";
    Order market_order{301, OrderSide::Buy, OrderType::Market, 0, std::nullopt, OrderStatus::New};
    assert(!market_order.is_valid());
    assert(market_order.get_validation_error().has_value());
    std::cout << "  - Error: " << market_order.get_validation_error().value() << "\n";

    bool threw = false;
    try {
        market_order.validate();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw && "validate() must throw std::invalid_argument for zero quantity");

    Order limit_order{302, OrderSide::Sell, OrderType::Limit, 0, 100.0, OrderStatus::New};
    assert(!limit_order.is_valid());

    std::cout << "  -> PASSED: Zero quantity rejected as expected.\n\n";
}

void test_missing_limit_price() {
    std::cout << "[Test 4] Testing missing limit price on Limit order...\n";
    Order limit_order{401, OrderSide::Buy, OrderType::Limit, 100, std::nullopt, OrderStatus::New};

    assert(!limit_order.is_valid());
    assert(limit_order.get_validation_error().has_value());
    std::cout << "  - Error: " << limit_order.get_validation_error().value() << "\n";

    bool threw = false;
    try {
        limit_order.validate();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw && "validate() must throw std::invalid_argument for missing limit price");

    std::cout << "  -> PASSED: Missing limit price rejected as expected.\n\n";
}

void test_invalid_limit_price() {
    std::cout << "[Test 5] Testing invalid (zero or negative) limit price...\n";

    // Limit price = 0.0
    Order zero_price{501, OrderSide::Buy, OrderType::Limit, 100, 0.0, OrderStatus::New};
    assert(!zero_price.is_valid());
    assert(zero_price.get_validation_error().has_value());
    std::cout << "  - Error (0.0 price): " << zero_price.get_validation_error().value() << "\n";

    bool threw_zero = false;
    try {
        zero_price.validate();
    } catch (const std::invalid_argument&) {
        threw_zero = true;
    }
    assert(threw_zero);

    // Limit price = -15.50
    Order neg_price{502, OrderSide::Sell, OrderType::Limit, 100, -15.50, OrderStatus::New};
    assert(!neg_price.is_valid());
    assert(neg_price.get_validation_error().has_value());
    std::cout << "  - Error (-15.50 price): " << neg_price.get_validation_error().value() << "\n";

    bool threw_neg = false;
    try {
        neg_price.validate();
    } catch (const std::invalid_argument&) {
        threw_neg = true;
    }
    assert(threw_neg);

    std::cout << "  -> PASSED: Zero and negative limit prices rejected as expected.\n\n";
}

void test_limit_price_on_market_order() {
    std::cout << "[Test 6] Testing limit price supplied for a Market order...\n";
    Order market_order{601, OrderSide::Buy, OrderType::Market, 100, 102.50, OrderStatus::New};

    assert(!market_order.is_valid());
    assert(market_order.get_validation_error().has_value());
    std::cout << "  - Error: " << market_order.get_validation_error().value() << "\n";

    bool threw = false;
    try {
        market_order.validate();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw && "validate() must throw when limit price is set on market order");

    std::cout << "  -> PASSED: Limit price on Market order rejected as expected.\n\n";
}

void test_enum_formatting() {
    std::cout << "[Test 7] Testing readable enum string formatting...\n";

    assert(to_string(OrderSide::Buy) == "BUY");
    assert(to_string(OrderSide::Sell) == "SELL");

    assert(to_string(OrderType::Market) == "MARKET");
    assert(to_string(OrderType::Limit) == "LIMIT");

    assert(to_string(OrderStatus::New) == "NEW");
    assert(to_string(OrderStatus::Filled) == "FILLED");
    assert(to_string(OrderStatus::Cancelled) == "CANCELLED");
    assert(to_string(OrderStatus::Rejected) == "REJECTED");

    std::cout << "  - OrderSide: " << to_string(OrderSide::Buy) << ", " << to_string(OrderSide::Sell) << "\n";
    std::cout << "  - OrderType: " << to_string(OrderType::Market) << ", " << to_string(OrderType::Limit) << "\n";
    std::cout << "  - OrderStatus: " << to_string(OrderStatus::New) << ", " << to_string(OrderStatus::Filled)
              << ", " << to_string(OrderStatus::Cancelled) << ", " << to_string(OrderStatus::Rejected) << "\n";

    std::cout << "  -> PASSED: All enums format to readable strings.\n\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   Running Order Unit Tests (Module 3)  \n";
    std::cout << "========================================\n\n";

    test_valid_buy_market_order();
    test_valid_sell_limit_order();
    test_zero_quantity();
    test_missing_limit_price();
    test_invalid_limit_price();
    test_limit_price_on_market_order();
    test_enum_formatting();

    std::cout << "========================================\n";
    std::cout << "   ALL ORDER TESTS PASSED SUCCESSFULLY!  \n";
    std::cout << "========================================\n";

    return 0;
}
