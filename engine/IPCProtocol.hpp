#pragma once

#include "Order.hpp"
#include "Portfolio.hpp"
#include "Trade.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace hft {
namespace ipc {

/**
 * @brief Current protocol version for the C++ <-> Python IPC pipe stream.
 */
inline constexpr std::uint32_t PROTOCOL_VERSION = 1;

/**
 * @brief Supported IPC message types.
 */
enum class MessageType {
    Version,
    Tick,
    Trade,
    Portfolio,
    End,
    Unknown
};

/**
 * @brief Converts MessageType enum to string literal representation.
 */
std::string to_string(MessageType type);

/**
 * @brief Converts string literal representation to MessageType enum.
 */
MessageType string_to_message_type(const std::string& type_str);

/**
 * @brief VERSION message: protocol version announcement.
 * Exact field order: VERSION \t version
 */
struct VersionMessage {
    std::uint32_t version{PROTOCOL_VERSION};
};

/**
 * @brief TICK message: sequential tick index and market price.
 * Exact field order: TICK \t version \t tick_number \t market_price
 */
struct TickMessage {
    std::uint32_t version{PROTOCOL_VERSION};
    std::uint64_t tick_number{0};
    double price{0.0};
};

/**
 * @brief TRADE message: executed trade details.
 * Exact field order: TRADE \t version \t trade_id \t buy_order_id \t sell_order_id \t price \t quantity
 */
struct TradeMessage {
    std::uint32_t version{PROTOCOL_VERSION};
    TradeId trade_id{0};
    OrderId buy_order_id{0};
    OrderId sell_order_id{0};
    double price{0.0};
    std::uint32_t quantity{0};
};

/**
 * @brief PORTFOLIO message: cash, position, cost basis, and P&L metrics.
 * Exact field order: PORTFOLIO \t version \t cash \t position_quantity \t average_entry_price \t realized_pnl \t unrealized_pnl \t total_pnl
 */
struct PortfolioMessage {
    std::uint32_t version{PROTOCOL_VERSION};
    double cash{0.0};
    std::uint32_t position_quantity{0};
    double average_entry_price{0.0};
    double realized_pnl{0.0};
    double unrealized_pnl{0.0};
    double total_pnl{0.0};
};

/**
 * @brief END message: clean termination announcement.
 * Exact field order: END \t version
 */
struct EndMessage {
    std::uint32_t version{PROTOCOL_VERSION};
};

// ============================================================================
// Serialization Functions
// ============================================================================

/**
 * @brief Serializes a VERSION message to a tab-separated string (without trailing newline).
 * @throws std::invalid_argument if version is 0.
 */
std::string serialize_version(std::uint32_t version = PROTOCOL_VERSION);

/**
 * @brief Serializes a TICK message to a tab-separated string (without trailing newline).
 * @throws std::invalid_argument if fields are invalid or non-finite.
 */
std::string serialize_tick(const TickMessage& msg);
std::string serialize_tick(std::uint64_t tick_number, double price, std::uint32_t version = PROTOCOL_VERSION);

/**
 * @brief Serializes a TRADE message to a tab-separated string (without trailing newline).
 * @throws std::invalid_argument if fields are invalid or non-finite.
 */
std::string serialize_trade(const TradeMessage& msg);
std::string serialize_trade(const Trade& trade, std::uint32_t version = PROTOCOL_VERSION);

/**
 * @brief Serializes a PORTFOLIO message to a tab-separated string (without trailing newline).
 * @throws std::invalid_argument if fields are invalid or non-finite.
 */
std::string serialize_portfolio(const PortfolioMessage& msg);
std::string serialize_portfolio(const Portfolio& portfolio, double current_market_price, std::uint32_t version = PROTOCOL_VERSION);

/**
 * @brief Serializes an END message to a tab-separated string (without trailing newline).
 * @throws std::invalid_argument if version is 0.
 */
std::string serialize_end(std::uint32_t version = PROTOCOL_VERSION);

// ============================================================================
// Deserialization / Parsing Functions (for C++ tests & verification)
// ============================================================================

/**
 * @brief Peeks at the message type of a raw protocol line.
 */
MessageType peek_message_type(const std::string& line);

/**
 * @brief Parses and validates a VERSION message line.
 * @throws std::invalid_argument if malformed, missing fields, or invalid numbers.
 */
VersionMessage parse_version(const std::string& line);

/**
 * @brief Parses and validates a TICK message line.
 * @throws std::invalid_argument if malformed, missing fields, or invalid numbers.
 */
TickMessage parse_tick(const std::string& line);

/**
 * @brief Parses and validates a TRADE message line.
 * @throws std::invalid_argument if malformed, missing fields, or invalid numbers.
 */
TradeMessage parse_trade(const std::string& line);

/**
 * @brief Parses and validates a PORTFOLIO message line.
 * @throws std::invalid_argument if malformed, missing fields, or invalid numbers.
 */
PortfolioMessage parse_portfolio(const std::string& line);

/**
 * @brief Parses and validates an END message line.
 * @throws std::invalid_argument if malformed, missing fields, or invalid numbers.
 */
EndMessage parse_end(const std::string& line);

// ============================================================================
// Protocol Writer Helper
// ============================================================================

/**
 * @brief Flushed, safe protocol writer that formats records to an output stream (default: std::cout).
 * Ensures every record is newline-terminated and flushed immediately to prevent buffering delay across pipes.
 */
class ProtocolWriter {
public:
    explicit ProtocolWriter(std::ostream& os = std::cout);

    void write_version(std::uint32_t version = PROTOCOL_VERSION);
    void write_tick(const TickMessage& msg);
    void write_tick(std::uint64_t tick_number, double price, std::uint32_t version = PROTOCOL_VERSION);
    void write_trade(const TradeMessage& msg);
    void write_trade(const Trade& trade, std::uint32_t version = PROTOCOL_VERSION);
    void write_portfolio(const PortfolioMessage& msg);
    void write_portfolio(const Portfolio& portfolio, double current_market_price, std::uint32_t version = PROTOCOL_VERSION);
    void write_end(std::uint32_t version = PROTOCOL_VERSION);

private:
    void write_line(const std::string& record);
    std::ostream& os_;
};

} // namespace ipc
} // namespace hft
