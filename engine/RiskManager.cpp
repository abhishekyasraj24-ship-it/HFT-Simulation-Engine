#include "RiskManager.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// RiskLimits::validate
// ---------------------------------------------------------------------------

void RiskLimits::validate() const {
    if (!std::isfinite(max_order_notional) || max_order_notional < 0.0) {
        throw std::invalid_argument(
            "RiskLimits: max_order_notional must be non-negative and finite "
            "(0.0 == unlimited)");
    }
    if (!std::isfinite(max_gross_exposure) || max_gross_exposure < 0.0) {
        throw std::invalid_argument(
            "RiskLimits: max_gross_exposure must be non-negative and finite "
            "(0.0 == unlimited)");
    }
    // Consistency: a single order cannot exceed the total allowed position
    if (max_order_quantity > 0 && max_position_quantity > 0 &&
        max_order_quantity > max_position_quantity) {
        throw std::invalid_argument(
            "RiskLimits: max_order_quantity (" +
            std::to_string(max_order_quantity) +
            ") cannot exceed max_position_quantity (" +
            std::to_string(max_position_quantity) + ")");
    }
}

// ---------------------------------------------------------------------------
// RiskManager
// ---------------------------------------------------------------------------

RiskManager::RiskManager(RiskLimits limits) : limits_(std::move(limits)) {
    limits_.validate();
}

double RiskManager::notional_price(const Order& order, double ref_market_price) {
    // For limit orders we know the worst-case execution price; use it.
    // For market orders we use the caller-supplied reference price.
    if (order.type == OrderType::Limit && order.limit_price.has_value()) {
        return order.limit_price.value();
    }
    return ref_market_price;
}

RiskCheckResult RiskManager::check_order(const Order&     order,
                                         const Portfolio& portfolio,
                                         double           ref_market_price) const {
    // ------------------------------------------------------------------
    // Check 1: Structural order validity
    // ------------------------------------------------------------------
    auto order_err = order.get_validation_error();
    if (order_err.has_value()) {
        return RiskCheckResult::reject("Order validation failed: " + order_err.value());
    }

    // ------------------------------------------------------------------
    // Check 2: Reference market price validity
    // ------------------------------------------------------------------
    if (!std::isfinite(ref_market_price) || ref_market_price <= 0.0) {
        return RiskCheckResult::reject(
            "Invalid reference market price: must be strictly positive and finite");
    }

    // ------------------------------------------------------------------
    // Check 3: Per-order quantity limit
    // ------------------------------------------------------------------
    if (limits_.max_order_quantity > 0 &&
        order.quantity > limits_.max_order_quantity) {
        std::ostringstream oss;
        oss << "Order quantity " << order.quantity
            << " exceeds max_order_quantity limit of " << limits_.max_order_quantity;
        return RiskCheckResult::reject(oss.str());
    }

    // ------------------------------------------------------------------
    // Check 4: Per-order notional limit
    // ------------------------------------------------------------------
    const double price_for_notional = notional_price(order, ref_market_price);
    const double order_notional =
        static_cast<double>(order.quantity) * price_for_notional;

    if (limits_.max_order_notional > 0.0 &&
        order_notional > limits_.max_order_notional) {
        std::ostringstream oss;
        oss << "Order notional $" << order_notional
            << " (qty=" << order.quantity
            << " * price=$" << price_for_notional
            << ") exceeds max_order_notional limit of $" << limits_.max_order_notional;
        return RiskCheckResult::reject(oss.str());
    }

    // ------------------------------------------------------------------
    // Side-specific checks
    // ------------------------------------------------------------------
    const std::uint32_t current_position = portfolio.get_position_quantity();

    if (order.side == OrderSide::Buy) {
        // Check 5a: Projected position must not exceed max_position_quantity
        // Use uint64 for the addition to avoid overflow before comparison.
        const std::uint64_t projected_position =
            static_cast<std::uint64_t>(current_position) +
            static_cast<std::uint64_t>(order.quantity);

        if (limits_.max_position_quantity > 0 &&
            projected_position > static_cast<std::uint64_t>(limits_.max_position_quantity)) {
            std::ostringstream oss;
            oss << "Buy would increase position to " << projected_position
                << " units, exceeding max_position_quantity limit of "
                << limits_.max_position_quantity
                << " (current position: " << current_position << ")";
            return RiskCheckResult::reject(oss.str());
        }

        // Check 7 (Buy): Projected gross exposure
        // Exposure = projected_position * ref_market_price
        if (limits_.max_gross_exposure > 0.0) {
            const double projected_exposure =
                static_cast<double>(projected_position) * ref_market_price;
            if (projected_exposure > limits_.max_gross_exposure) {
                std::ostringstream oss;
                oss << "Buy would create gross exposure $" << projected_exposure
                    << " (projected_position=" << projected_position
                    << " * ref_price=$" << ref_market_price
                    << "), exceeding max_gross_exposure limit of $"
                    << limits_.max_gross_exposure;
                return RiskCheckResult::reject(oss.str());
            }
        }

    } else { // OrderSide::Sell
        // Check 6: Projected position must not go negative (long-only)
        if (order.quantity > current_position) {
            std::ostringstream oss;
            oss << "Sell of " << order.quantity
                << " units would exceed current long position of "
                << current_position << " units (short selling is not permitted)";
            return RiskCheckResult::reject(oss.str());
        }

        // Check 7 (Sell): Projected gross exposure
        // After the sell, position shrinks — exposure only increases on buys,
        // but we still validate the post-sell exposure for completeness.
        if (limits_.max_gross_exposure > 0.0) {
            const std::uint32_t projected_position =
                current_position - order.quantity;
            const double projected_exposure =
                static_cast<double>(projected_position) * ref_market_price;
            // A sell can only reduce exposure, so this will pass unless the
            // limit was already breached. We keep the check symmetric.
            if (projected_exposure > limits_.max_gross_exposure) {
                std::ostringstream oss;
                oss << "Post-sell gross exposure $" << projected_exposure
                    << " still exceeds max_gross_exposure limit of $"
                    << limits_.max_gross_exposure;
                return RiskCheckResult::reject(oss.str());
            }
        }
    }

    return RiskCheckResult::ok();
}
