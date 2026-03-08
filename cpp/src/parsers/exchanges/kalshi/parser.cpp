#include "trading/parsers/exchanges/kalshi/parser.hpp"

#include <charconv>
#include <cmath>
#include <limits>

#include <simdjson.h>

namespace trading::parsers::exchanges::kalshi {

namespace {

// Kalshi on-demand message type strings.
constexpr std::string_view kTypeOrderbook = "orderbook_snapshot";
constexpr std::string_view kTypeOrderbookDelta = "orderbook_delta";
constexpr std::string_view kTypeTrade = "trade";

bool get_string(simdjson::ondemand::object& obj, std::string_view key, std::string_view& out) noexcept {
    auto result = obj.find_field_unordered(key).get_string();
    if (result.error() != simdjson::SUCCESS) {
        return false;
    }
    out = result.value_unsafe();
    return true;
}

bool get_int64(simdjson::ondemand::object& obj, std::string_view key, std::int64_t& out) noexcept {
    auto result = obj.find_field_unordered(key).get_int64();
    if (result.error() != simdjson::SUCCESS) {
        return false;
    }
    out = result.value_unsafe();
    return true;
}

bool parse_non_negative_decimal_string_to_int64(std::string_view value, std::int64_t& out) noexcept {
    if (value.empty()) {
        return false;
    }
    if (value.front() == '-') {
        return false;
    }

    const std::size_t dot = value.find('.');
    const std::string_view int_part = (dot == std::string_view::npos) ? value : value.substr(0, dot);
    const std::string_view frac_part =
        (dot == std::string_view::npos) ? std::string_view{} : value.substr(dot + 1);

    if (int_part.empty()) {
        return false;
    }
    for (char digit_char : int_part) {
        if (digit_char < '0' || digit_char > '9') {
            return false;
        }
    }
    for (char frac_char : frac_part) {
        if (frac_char != '0') {
            return false;
        }
    }

    std::uint64_t parsed = 0;
    const auto [ptr, ec] = std::from_chars(int_part.data(), int_part.data() + int_part.size(), parsed);
    if (ec != std::errc{} || ptr != int_part.data() + int_part.size() ||
        parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }

    out = static_cast<std::int64_t>(parsed);
    return true;
}

bool get_uint64(simdjson::ondemand::object& obj, std::string_view key, std::uint64_t& out) noexcept {
    auto result = obj.find_field_unordered(key).get_uint64();
    if (result.error() == simdjson::SUCCESS) {
        out = result.value_unsafe();
        return true;
    }

    std::int64_t signed_value = 0;
    if (!get_int64(obj, key, signed_value) || signed_value < 0) {
        return false;
    }
    out = static_cast<std::uint64_t>(signed_value);
    return true;
}

bool get_lot_count(simdjson::ondemand::object& msg, std::int64_t& out) noexcept {
    if (get_int64(msg, "count", out) || get_int64(msg, "count_fp", out)) {
        return out >= 0;
    }

    auto count_fp_double = msg.find_field_unordered("count_fp").get_double();
    if (count_fp_double.error() == simdjson::SUCCESS) {
        const double count = count_fp_double.value_unsafe();
        if (count < 0) {
            return false;
        }
        double integer_part = 0.0;
        if (std::modf(count, &integer_part) != 0.0 ||
            integer_part > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        out = static_cast<std::int64_t>(integer_part);
        return true;
    }

    std::string_view count_fp_text;
    if (get_string(msg, "count_fp", count_fp_text)) {
        return parse_non_negative_decimal_string_to_int64(count_fp_text, out);
    }

    return false;
}

bool get_object(simdjson::ondemand::object& obj,
                std::string_view key,
                simdjson::ondemand::object& out) noexcept {
    auto result = obj.find_field_unordered(key).get_object();
    if (result.error() != simdjson::SUCCESS) {
        return false;
    }
    out = result.value_unsafe();
    return true;
}

bool get_array(simdjson::ondemand::object& obj,
               std::string_view key,
               simdjson::ondemand::array& out) noexcept {
    auto result = obj.find_field_unordered(key).get_array();
    if (result.error() != simdjson::SUCCESS) {
        return false;
    }
    out = result.value_unsafe();
    return true;
}

internal::Side parse_book_side(std::string_view side_token) {
    if (side_token == "yes" || side_token == "bid") {
        return internal::Side::kBid;
    }
    if (side_token == "no" || side_token == "ask") {
        return internal::Side::kAsk;
    }
    return internal::Side::kUnknown;
}

internal::Side parse_trade_side(std::string_view side_token) {
    if (side_token == "yes" || side_token == "buy") {
        return internal::Side::kBuy;
    }
    if (side_token == "no" || side_token == "sell") {
        return internal::Side::kSell;
    }
    return internal::Side::kUnknown;
}

void set_sequence_from_root(simdjson::ondemand::object& root, internal::NormalizedEvent& event) {
    std::uint64_t seq = 0;
    if (get_uint64(root, "seq", seq) || get_uint64(root, "seq_id", seq)) {
        event.raw_sequence_id = seq;
        event.meta.sequence_id = seq;
    }
}

void set_sequence_from_msg(simdjson::ondemand::object& msg, internal::NormalizedEvent& event) {
    std::uint64_t seq = 0;
    if (get_uint64(msg, "seq", seq) || get_uint64(msg, "seq_id", seq)) {
        event.raw_sequence_id = seq;
        event.meta.sequence_id = seq;
        return;
    }
    if (event.raw_sequence_id.has_value()) {
        event.meta.sequence_id = event.raw_sequence_id.value();
    }
}

bool parse_levels(simdjson::ondemand::array& arr, std::vector<internal::Level>& out) noexcept {
    for (auto level_val : arr) {
        auto level_arr_result = level_val.get_array();
        if (level_arr_result.error() != simdjson::SUCCESS) {
            return false;
        }
        auto iter = level_arr_result.value_unsafe().begin();
        if (iter.error() != simdjson::SUCCESS) {
            return false;
        }

        internal::Level level{};

        auto price_result = (*iter).get_int64();
        if (price_result.error() != simdjson::SUCCESS) {
            return false;
        }
        level.price_ticks = price_result.value_unsafe();

        ++iter;
        if (iter.error() != simdjson::SUCCESS) {
            return false;
        }

        auto qty_result = (*iter).get_int64();
        if (qty_result.error() != simdjson::SUCCESS) {
            return false;
        }
        level.qty_lots = qty_result.value_unsafe();

        if (level.price_ticks < 0 || level.qty_lots < 0) {
            return false;
        }
        out.push_back(level);
    }
    return true;
}

ParseResult<internal::NormalizedEvent>
parse_snapshot(simdjson::ondemand::object& msg, internal::NormalizedEvent event) {
    if (!event.raw_sequence_id.has_value()) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }
    event.type = internal::EventType::kSnapshot;

    simdjson::ondemand::array yes_levels;
    if (!get_array(msg, "yes", yes_levels)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }

    internal::SnapshotData snapshot{};
    if (!parse_levels(yes_levels, snapshot.bids)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kInvalidField);
    }

    simdjson::ondemand::array no_levels;
    if (!get_array(msg, "no", no_levels)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }
    if (!parse_levels(no_levels, snapshot.asks)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kInvalidField);
    }
    event.data = std::move(snapshot);
    return ParseResult<internal::NormalizedEvent>::success(std::move(event));
}

ParseResult<internal::NormalizedEvent>
parse_delta(simdjson::ondemand::object& msg,
            internal::NormalizedEvent event,
            bool strict_field_validation) {
    if (!event.raw_sequence_id.has_value()) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }
    event.type = internal::EventType::kDelta;

    std::int64_t price = 0;
    std::int64_t delta_qty = 0;
    if (!get_int64(msg, "price", price) || !get_int64(msg, "delta", delta_qty)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }
    if (price < 0) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kInvalidField);
    }

    std::string_view side_token;
    if (!get_string(msg, "side", side_token)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }

    internal::DeltaData delta{};
    delta.side = parse_book_side(side_token);
    if (delta.side == internal::Side::kUnknown && strict_field_validation) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kInvalidField);
    }
    delta.price_ticks = price;
    delta.delta_qty_lots = delta_qty;
    event.data = delta;
    return ParseResult<internal::NormalizedEvent>::success(std::move(event));
}

ParseResult<internal::NormalizedEvent>
parse_trade(simdjson::ondemand::object& msg,
            internal::NormalizedEvent event,
            bool strict_field_validation) {
    set_sequence_from_msg(msg, event);
    event.type = internal::EventType::kTrade;

    std::int64_t price = 0;
    std::int64_t qty = 0;
    if ((!get_int64(msg, "yes_price", price) && !get_int64(msg, "price", price)) || !get_lot_count(msg, qty)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }
    if (price < 0 || qty < 0) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kInvalidField);
    }

    internal::TradeData trade{};
    trade.price_ticks = price;
    trade.qty_lots = qty;

    std::string_view taker_side;
    if (!get_string(msg, "taker_side", taker_side)) {
        if (strict_field_validation) {
            return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
        }
    } else {
        trade.aggressor = parse_trade_side(taker_side);
        if (trade.aggressor == internal::Side::kUnknown && strict_field_validation) {
            return ParseResult<internal::NormalizedEvent>::failure(ParseError::kInvalidField);
        }
    }

    std::string_view trade_id_view;
    if (get_string(msg, "trade_id", trade_id_view)) {
        trade.trade_id = std::string{trade_id_view};
    }

    event.data = trade;
    return ParseResult<internal::NormalizedEvent>::success(std::move(event));
}

} // namespace

ParseResult<internal::NormalizedEvent> Parser::parse(const internal::RouterFrame& frame) const {
    simdjson::padded_string padded{frame.raw_payload_view};
    simdjson::ondemand::parser json_parser;
    auto doc = json_parser.iterate(padded);
    if (doc.error() != simdjson::SUCCESS) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kInvalidJson);
    }

    auto root = doc.get_object();
    if (root.error() != simdjson::SUCCESS) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kInvalidJson);
    }
    auto obj = root.value_unsafe();

    std::string_view type_sv;
    if (!get_string(obj, "type", type_sv)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }

    internal::NormalizedEvent event{};
    event.meta.exchange    = internal::ExchangeId::kKalshi;
    event.meta.recv_ns     = frame.recv_ns;
    event.market_ticker    = frame.market_ticker;
    event.raw_sequence_id  = frame.sequence_id;
    if (frame.sequence_id.has_value()) {
        event.meta.sequence_id = frame.sequence_id.value();
    }
    event.raw_payload      = std::string{frame.raw_payload_view};
    set_sequence_from_root(obj, event);

    simdjson::ondemand::object msg;
    if ((type_sv == kTypeOrderbook || type_sv == kTypeOrderbookDelta || type_sv == kTypeTrade) &&
        !get_object(obj, "msg", msg)) {
        return ParseResult<internal::NormalizedEvent>::failure(ParseError::kMissingField);
    }

    if (type_sv == kTypeOrderbook) {
        return parse_snapshot(msg, std::move(event));
    }
    if (type_sv == kTypeOrderbookDelta) {
        return parse_delta(msg, std::move(event), strict_field_validation_);
    }
    if (type_sv == kTypeTrade) {
        return parse_trade(msg, std::move(event), strict_field_validation_);
    }
    return ParseResult<internal::NormalizedEvent>::failure(ParseError::kUnsupportedMessageType);
}

} // namespace trading::parsers::exchanges::kalshi
