#include "IPCProtocol.hpp"

#include <cctype>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace hft {
namespace ipc {

namespace {

/**
 * @brief Formats a double to fixed 6-decimal notation, ensuring it is strictly finite.
 */
std::string format_finite_double(double val, const std::string& field_name) {
    if (!std::isfinite(val)) {
        throw std::invalid_argument("Protocol error: " + field_name + " must be a finite floating-point number");
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << val;
    return oss.str();
}

/**
 * @brief Strips trailing CR/LF characters from a string.
 */
std::string strip_line_endings(const std::string& str) {
    std::string s = str;
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    return s;
}

/**
 * @brief Splits a string by tab character ('\t').
 */
std::vector<std::string> split_by_tab(const std::string& line) {
    std::vector<std::string> tokens;
    std::string clean = strip_line_endings(line);
    if (clean.empty()) {
        return tokens;
    }
    std::size_t start = 0;
    std::size_t end = clean.find('\t');
    while (end != std::string::npos) {
        tokens.push_back(clean.substr(start, end - start));
        start = end + 1;
        end = clean.find('\t', start);
    }
    tokens.push_back(clean.substr(start));
    return tokens;
}

/**
 * @brief Safely parses an unsigned 64-bit integer from a token string.
 */
std::uint64_t parse_uint64(const std::string& token, const std::string& field_name) {
    if (token.empty()) {
        throw std::invalid_argument("Protocol error: " + field_name + " cannot be empty");
    }
    for (char c : token) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw std::invalid_argument("Protocol error: " + field_name + " contains non-digit character: '" + token + "'");
        }
    }
    try {
        std::size_t pos = 0;
        unsigned long long val = std::stoull(token, &pos);
        if (pos != token.size()) {
            throw std::invalid_argument("Protocol error: " + field_name + " has trailing characters");
        }
        return static_cast<std::uint64_t>(val);
    } catch (const std::exception&) {
        throw std::invalid_argument("Protocol error: " + field_name + " value out of range: '" + token + "'");
    }
}

/**
 * @brief Safely parses an unsigned 32-bit integer from a token string.
 */
std::uint32_t parse_uint32(const std::string& token, const std::string& field_name) {
    std::uint64_t val = parse_uint64(token, field_name);
    if (val > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Protocol error: " + field_name + " exceeds 32-bit integer limit: '" + token + "'");
    }
    return static_cast<std::uint32_t>(val);
}

/**
 * @brief Safely parses a finite double from a token string.
 */
double parse_finite_double(const std::string& token, const std::string& field_name) {
    if (token.empty()) {
        throw std::invalid_argument("Protocol error: " + field_name + " cannot be empty");
    }
    // Reject explicit NaN or Infinity strings
    std::string lower;
    lower.reserve(token.size());
    for (char c : token) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "nan" || lower == "-nan" || lower == "+nan" ||
        lower == "inf" || lower == "-inf" || lower == "+inf" ||
        lower == "infinity" || lower == "-infinity" || lower == "+infinity") {
        throw std::invalid_argument("Protocol error: " + field_name + " must be a finite number; got: '" + token + "'");
    }

    try {
        std::size_t pos = 0;
        double val = std::stod(token, &pos);
        if (pos != token.size()) {
            throw std::invalid_argument("Protocol error: " + field_name + " has trailing non-numeric characters: '" + token + "'");
        }
        if (!std::isfinite(val)) {
            throw std::invalid_argument("Protocol error: " + field_name + " must be finite");
        }
        return val;
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("Protocol error: " + field_name + " is not a valid floating-point number: '" + token + "'");
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("Protocol error: " + field_name + " floating-point value out of range: '" + token + "'");
    }
}

} // anonymous namespace

std::string to_string(MessageType type) {
    switch (type) {
        case MessageType::Version:   return "VERSION";
        case MessageType::Tick:      return "TICK";
        case MessageType::Trade:     return "TRADE";
        case MessageType::Portfolio: return "PORTFOLIO";
        case MessageType::End:       return "END";
        case MessageType::Unknown:   return "UNKNOWN";
    }
    return "UNKNOWN";
}

MessageType string_to_message_type(const std::string& type_str) {
    if (type_str == "VERSION")   return MessageType::Version;
    if (type_str == "TICK")      return MessageType::Tick;
    if (type_str == "TRADE")     return MessageType::Trade;
    if (type_str == "PORTFOLIO") return MessageType::Portfolio;
    if (type_str == "END")       return MessageType::End;
    return MessageType::Unknown;
}

// ============================================================================
// Serialization Implementations
// ============================================================================

std::string serialize_version(std::uint32_t version) {
    if (version == 0) {
        throw std::invalid_argument("Protocol error: version must be greater than zero");
    }
    return "VERSION\t" + std::to_string(version);
}

std::string serialize_tick(const TickMessage& msg) {
    if (msg.version == 0) {
        throw std::invalid_argument("Protocol error: version must be greater than zero");
    }
    if (msg.tick_number == 0) {
        throw std::invalid_argument("Protocol error: tick_number must be strictly positive (>= 1)");
    }
    if (!std::isfinite(msg.price) || msg.price <= 0.0) {
        throw std::invalid_argument("Protocol error: tick price must be a finite, strictly positive number");
    }
    return "TICK\t" + std::to_string(msg.version) + "\t" +
           std::to_string(msg.tick_number) + "\t" +
           format_finite_double(msg.price, "price");
}

std::string serialize_tick(std::uint64_t tick_number, double price, std::uint32_t version) {
    return serialize_tick(TickMessage{version, tick_number, price});
}

std::string serialize_trade(const TradeMessage& msg) {
    if (msg.version == 0) {
        throw std::invalid_argument("Protocol error: version must be greater than zero");
    }
    if (msg.trade_id == 0) {
        throw std::invalid_argument("Protocol error: trade_id must be strictly positive (>= 1)");
    }
    if (msg.buy_order_id == 0 || msg.sell_order_id == 0) {
        throw std::invalid_argument("Protocol error: buy_order_id and sell_order_id must be strictly positive");
    }
    if (!std::isfinite(msg.price) || msg.price <= 0.0) {
        throw std::invalid_argument("Protocol error: trade price must be a finite, strictly positive number");
    }
    if (msg.quantity == 0) {
        throw std::invalid_argument("Protocol error: trade quantity must be strictly greater than zero");
    }
    return "TRADE\t" + std::to_string(msg.version) + "\t" +
           std::to_string(msg.trade_id) + "\t" +
           std::to_string(msg.buy_order_id) + "\t" +
           std::to_string(msg.sell_order_id) + "\t" +
           format_finite_double(msg.price, "price") + "\t" +
           std::to_string(msg.quantity);
}

std::string serialize_trade(const Trade& trade, std::uint32_t version) {
    return serialize_trade(TradeMessage{version, trade.trade_id, trade.buy_order_id, trade.sell_order_id, trade.price, trade.quantity});
}

std::string serialize_portfolio(const PortfolioMessage& msg) {
    if (msg.version == 0) {
        throw std::invalid_argument("Protocol error: version must be greater than zero");
    }
    if (!std::isfinite(msg.cash)) {
        throw std::invalid_argument("Protocol error: cash must be a finite number");
    }
    if (!std::isfinite(msg.average_entry_price) || msg.average_entry_price < 0.0) {
        throw std::invalid_argument("Protocol error: average_entry_price must be a finite, non-negative number");
    }
    if (!std::isfinite(msg.realized_pnl) || !std::isfinite(msg.unrealized_pnl) || !std::isfinite(msg.total_pnl)) {
        throw std::invalid_argument("Protocol error: P&L metrics must be finite numbers");
    }
    return "PORTFOLIO\t" + std::to_string(msg.version) + "\t" +
           format_finite_double(msg.cash, "cash") + "\t" +
           std::to_string(msg.position_quantity) + "\t" +
           format_finite_double(msg.average_entry_price, "average_entry_price") + "\t" +
           format_finite_double(msg.realized_pnl, "realized_pnl") + "\t" +
           format_finite_double(msg.unrealized_pnl, "unrealized_pnl") + "\t" +
           format_finite_double(msg.total_pnl, "total_pnl");
}

std::string serialize_portfolio(const Portfolio& portfolio, double current_market_price, std::uint32_t version) {
    if (!std::isfinite(current_market_price) || current_market_price <= 0.0) {
        throw std::invalid_argument("Protocol error: current_market_price must be a finite, strictly positive number");
    }
    PortfolioMessage msg;
    msg.version = version;
    msg.cash = portfolio.get_cash();
    msg.position_quantity = portfolio.get_position_quantity();
    msg.average_entry_price = portfolio.get_average_entry_price();
    msg.realized_pnl = portfolio.get_realized_pnl();
    msg.unrealized_pnl = portfolio.get_unrealized_pnl(current_market_price);
    msg.total_pnl = portfolio.get_total_pnl(current_market_price);
    return serialize_portfolio(msg);
}

std::string serialize_end(std::uint32_t version) {
    if (version == 0) {
        throw std::invalid_argument("Protocol error: version must be greater than zero");
    }
    return "END\t" + std::to_string(version);
}

// ============================================================================
// Deserialization / Parsing Implementations
// ============================================================================

MessageType peek_message_type(const std::string& line) {
    auto tokens = split_by_tab(line);
    if (tokens.empty()) {
        return MessageType::Unknown;
    }
    return string_to_message_type(tokens[0]);
}

VersionMessage parse_version(const std::string& line) {
    auto tokens = split_by_tab(line);
    if (tokens.size() != 2) {
        throw std::invalid_argument("Protocol error: VERSION message must have exactly 2 fields, got " + std::to_string(tokens.size()));
    }
    if (tokens[0] != "VERSION") {
        throw std::invalid_argument("Protocol error: expected message type 'VERSION', got '" + tokens[0] + "'");
    }
    std::uint32_t ver = parse_uint32(tokens[1], "version");
    if (ver == 0) {
        throw std::invalid_argument("Protocol error: version must be greater than zero");
    }
    return VersionMessage{ver};
}

TickMessage parse_tick(const std::string& line) {
    auto tokens = split_by_tab(line);
    if (tokens.size() != 4) {
        throw std::invalid_argument("Protocol error: TICK message must have exactly 4 fields, got " + std::to_string(tokens.size()));
    }
    if (tokens[0] != "TICK") {
        throw std::invalid_argument("Protocol error: expected message type 'TICK', got '" + tokens[0] + "'");
    }
    std::uint32_t ver = parse_uint32(tokens[1], "version");
    std::uint64_t tick_num = parse_uint64(tokens[2], "tick_number");
    if (tick_num == 0) {
        throw std::invalid_argument("Protocol error: tick_number must be >= 1");
    }
    double price = parse_finite_double(tokens[3], "price");
    if (price <= 0.0) {
        throw std::invalid_argument("Protocol error: tick price must be strictly positive");
    }
    return TickMessage{ver, tick_num, price};
}

TradeMessage parse_trade(const std::string& line) {
    auto tokens = split_by_tab(line);
    if (tokens.size() != 7) {
        throw std::invalid_argument("Protocol error: TRADE message must have exactly 7 fields, got " + std::to_string(tokens.size()));
    }
    if (tokens[0] != "TRADE") {
        throw std::invalid_argument("Protocol error: expected message type 'TRADE', got '" + tokens[0] + "'");
    }
    std::uint32_t ver = parse_uint32(tokens[1], "version");
    TradeId tid = parse_uint64(tokens[2], "trade_id");
    OrderId bid = parse_uint64(tokens[3], "buy_order_id");
    OrderId sid = parse_uint64(tokens[4], "sell_order_id");
    double price = parse_finite_double(tokens[5], "price");
    if (price <= 0.0) {
        throw std::invalid_argument("Protocol error: trade price must be strictly positive");
    }
    std::uint32_t qty = parse_uint32(tokens[6], "quantity");
    if (qty == 0) {
        throw std::invalid_argument("Protocol error: trade quantity must be strictly positive");
    }
    return TradeMessage{ver, tid, bid, sid, price, qty};
}

PortfolioMessage parse_portfolio(const std::string& line) {
    auto tokens = split_by_tab(line);
    if (tokens.size() != 8) {
        throw std::invalid_argument("Protocol error: PORTFOLIO message must have exactly 8 fields, got " + std::to_string(tokens.size()));
    }
    if (tokens[0] != "PORTFOLIO") {
        throw std::invalid_argument("Protocol error: expected message type 'PORTFOLIO', got '" + tokens[0] + "'");
    }
    std::uint32_t ver = parse_uint32(tokens[1], "version");
    double cash = parse_finite_double(tokens[2], "cash");
    std::uint32_t pos_qty = parse_uint32(tokens[3], "position_quantity");
    double avg_price = parse_finite_double(tokens[4], "average_entry_price");
    if (avg_price < 0.0) {
        throw std::invalid_argument("Protocol error: average_entry_price must be non-negative");
    }
    double realized = parse_finite_double(tokens[5], "realized_pnl");
    double unrealized = parse_finite_double(tokens[6], "unrealized_pnl");
    double total = parse_finite_double(tokens[7], "total_pnl");
    return PortfolioMessage{ver, cash, pos_qty, avg_price, realized, unrealized, total};
}

EndMessage parse_end(const std::string& line) {
    auto tokens = split_by_tab(line);
    if (tokens.size() != 2) {
        throw std::invalid_argument("Protocol error: END message must have exactly 2 fields, got " + std::to_string(tokens.size()));
    }
    if (tokens[0] != "END") {
        throw std::invalid_argument("Protocol error: expected message type 'END', got '" + tokens[0] + "'");
    }
    std::uint32_t ver = parse_uint32(tokens[1], "version");
    if (ver == 0) {
        throw std::invalid_argument("Protocol error: version must be greater than zero");
    }
    return EndMessage{ver};
}

// ============================================================================
// ProtocolWriter Implementation
// ============================================================================

ProtocolWriter::ProtocolWriter(std::ostream& os) : os_(os) {}

void ProtocolWriter::write_line(const std::string& record) {
    os_ << record << "\n";
    os_.flush();
}

void ProtocolWriter::write_version(std::uint32_t version) {
    write_line(serialize_version(version));
}

void ProtocolWriter::write_tick(const TickMessage& msg) {
    write_line(serialize_tick(msg));
}

void ProtocolWriter::write_tick(std::uint64_t tick_number, double price, std::uint32_t version) {
    write_line(serialize_tick(tick_number, price, version));
}

void ProtocolWriter::write_trade(const TradeMessage& msg) {
    write_line(serialize_trade(msg));
}

void ProtocolWriter::write_trade(const Trade& trade, std::uint32_t version) {
    write_line(serialize_trade(trade, version));
}

void ProtocolWriter::write_portfolio(const PortfolioMessage& msg) {
    write_line(serialize_portfolio(msg));
}

void ProtocolWriter::write_portfolio(const Portfolio& portfolio, double current_market_price, std::uint32_t version) {
    write_line(serialize_portfolio(portfolio, current_market_price, version));
}

void ProtocolWriter::write_end(std::uint32_t version) {
    write_line(serialize_end(version));
}

} // namespace ipc
} // namespace hft
