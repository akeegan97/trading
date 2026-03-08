#include "trading/decode/exchanges/kalshi/extractor.hpp"

#include <simdjson.h>

namespace trading::decode::exchanges::kalshi {
namespace {

constexpr std::string_view kTypeOrderbookSnapshot = "orderbook_snapshot";
constexpr std::string_view kTypeOrderbookDelta = "orderbook_delta";
constexpr std::string_view kTypeTrade = "trade";

bool get_string(simdjson::ondemand::object& obj, std::string_view key, std::string_view& out) {
    auto result = obj.find_field_unordered(key).get_string();
    if (result.error() != simdjson::SUCCESS) {
        return false;
    }
    out = result.value_unsafe();
    return true;
}

bool get_uint64(simdjson::ondemand::object& obj, std::string_view key, std::uint64_t& out) {
    auto result = obj.find_field_unordered(key).get_uint64();
    if (result.error() == simdjson::SUCCESS) {
        out = result.value_unsafe();
        return true;
    }

    auto signed_result = obj.find_field_unordered(key).get_int64();
    if (signed_result.error() != simdjson::SUCCESS || signed_result.value_unsafe() < 0) {
        return false;
    }
    out = static_cast<std::uint64_t>(signed_result.value_unsafe());
    return true;
}

bool get_object(simdjson::ondemand::object& obj, std::string_view key,
                simdjson::ondemand::object& out) {
    auto result = obj.find_field_unordered(key).get_object();
    if (result.error() != simdjson::SUCCESS) {
        return false;
    }
    out = result.value_unsafe();
    return true;
}

bool extract_sequence(simdjson::ondemand::object& root, simdjson::ondemand::object& msg,
                      std::optional<std::uint64_t>& seq_id) {
    std::uint64_t parsed_seq = 0;
    if (get_uint64(root, "seq", parsed_seq) || get_uint64(root, "seq_id", parsed_seq) ||
        get_uint64(msg, "seq", parsed_seq) || get_uint64(msg, "seq_id", parsed_seq)) {
        seq_id = parsed_seq;
        return true;
    }
    return false;
}

} // namespace

ExtractedFields extract(const ingest::RawFrame& frame) {
    ExtractedFields fields{
        .status = DecodeStatus::kMissingField,
        .market_ticker = frame.market_ticker,
        .seq_id = frame.seq_id,
        .recv_timestamp = frame.recv_timestamp,
        .source = frame.source,
    };
    if (!fields.market_ticker.empty()) {
        fields.status = DecodeStatus::kOk;
        return fields;
    }

    simdjson::padded_string padded{frame.payload};
    simdjson::ondemand::parser parser;
    auto doc = parser.iterate(padded);
    if (doc.error() != simdjson::SUCCESS) {
        return fields;
    }

    auto root_result = doc.get_object();
    if (root_result.error() != simdjson::SUCCESS) {
        return fields;
    }
    auto root = root_result.value_unsafe();

    std::string_view type;
    if (!get_string(root, "type", type)) {
        return fields;
    }

    if (type != kTypeOrderbookSnapshot && type != kTypeOrderbookDelta && type != kTypeTrade) {
        fields.status = DecodeStatus::kUnsupportedMessage;
        return fields;
    }

    simdjson::ondemand::object msg;
    if (!get_object(root, "msg", msg)) {
        return fields;
    }

    std::string_view market_ticker;
    if (!get_string(msg, "market_ticker", market_ticker) || market_ticker.empty()) {
        return fields;
    }

    fields.market_ticker = std::string{market_ticker};
    (void)extract_sequence(root, msg, fields.seq_id);
    fields.recv_timestamp = frame.recv_timestamp;
    fields.source = frame.source;
    fields.status = DecodeStatus::kOk;
    return fields;
}

} // namespace trading::decode::exchanges::kalshi
