#pragma once

#include "Order.hpp"
#include "Portfolio.hpp"

#include <cmath>
#include <cstdint>
#include <string>

/**
 * @brief Configurable limits applied by the RiskManager during pre-trade checks.
 *
 * All limits are in terms of the simulated asset and cash units used throughout
 * the engine. Limits are validated on construction; nonsensical values throw.
 *
 * Limit semantics:
 * - max_order_quantity    : Maximum quantity allowed in a single order (inclusive).
 * - max_order_notional    : Maximum cash value of a single order (qty * price, inclusive).
 * - max_position_quantity : Maximum long position size allowed (inclusive).
 * - max_gross_exposure    : Maximum gross position value (position_qty * ref_price, inclusive).
 *
 * Reference-price policy for exposure and notional calculations:
 * - Limit orders:  use the order's limit_price (worst-case execution price).
 * - Market orders: use the caller-supplied reference market price (required; must be > 0).
 */
struct RiskLimits {
    std::uint32_t max_order_quantity{0};   ///< 0 means "unlimited"
    double        max_order_notional{0.0}; ///< 0.0 means "unlimited"
    std::uint32_t max_position_quantity{0};///< 0 means "unlimited"
    double        max_gross_exposure{0.0}; ///< 0.0 means "unlimited"

    /**
     * @brief Validates that configured limits are internally consistent.
     *
     * Rules:
     * - max_order_notional must be non-negative and finite (0.0 == unlimited).
     * - max_gross_exposure must be non-negative and finite (0.0 == unlimited).
     * - If both max_order_quantity and max_position_quantity are non-zero,
     *   max_order_quantity <= max_position_quantity (you can't send an order bigger
     *   than your total allowed position).
     *
     * @throws std::invalid_argument if any limit is invalid.
     */
    void validate() const;
};

/**
 * @brief Result returned by RiskManager::check_order().
 */
struct RiskCheckResult {
    bool        approved{false};     ///< true if the order passed all risk checks
    std::string rejection_reason;    ///< Non-empty only when approved == false

    /** Convenience factory for an approved result. */
    static RiskCheckResult ok() {
        return {true, ""};
    }

    /** Convenience factory for a rejected result. */
    static RiskCheckResult reject(std::string reason) {
        return {false, std::move(reason)};
    }
};

/**
 * @brief Pre-trade risk gate that validates proposed orders against configurable limits.
 *
 * The RiskManager is intentionally stateless with respect to order flow: it does NOT
 * modify the portfolio, submit orders, or track historical order state. It performs a
 * pure read-only inspection of the proposed order and the current portfolio snapshot.
 *
 * Checks performed (in order):
 *  1. Order structural validity (via Order::get_validation_error).
 *  2. Reference market price validity (must be finite and > 0).
 *  3. Per-order quantity limit.
 *  4. Per-order notional limit.
 *  5. For Buy: projected position does not exceed max_position_quantity.
 *  6. For Sell: projected position is non-negative (long-only portfolio).
 *  7. Projected gross exposure does not exceed max_gross_exposure.
 */
class RiskManager {
public:
    /**
     * @brief Constructs a RiskManager with validated risk limits.
     * @param limits The configured risk limits (validated on construction).
     * @throws std::invalid_argument if limits are invalid.
     */
    explicit RiskManager(RiskLimits limits);

    /**
     * @brief Performs pre-trade risk validation for a proposed order.
     *
     * This method is read-only: it does NOT modify the portfolio or book any trades.
     *
     * @param order             The proposed order to validate.
     * @param portfolio         Current portfolio state (read-only).
     * @param ref_market_price  Reference market price for exposure and notional
     *                          calculations. Used directly for market orders; for
     *                          limit orders the limit_price is used for notional
     *                          but ref_market_price is used for exposure projection.
     *                          Must be strictly positive and finite.
     * @return RiskCheckResult  Approved or rejected with reason.
     */
    RiskCheckResult check_order(const Order&     order,
                                const Portfolio& portfolio,
                                double           ref_market_price) const;

    /** Returns the active risk limits. */
    const RiskLimits& get_limits() const { return limits_; }

private:
    RiskLimits limits_;

    /**
     * @brief Selects the price to use for notional calculation.
     * Limit orders → limit_price. Market orders → ref_market_price.
     */
    static double notional_price(const Order& order, double ref_market_price);
};
