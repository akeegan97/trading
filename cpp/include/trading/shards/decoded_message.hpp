#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace trading::shards {

enum class DecodedMessageType : std::uint8_t {
    kUnknown,
    kSnapshot,
    kDelta,
    kTrade,
};

struct PriceLevel {
    double price{0.0};
    double quantity{0.0};
};

struct SnapshotPayload {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

struct DeltaPayload {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

struct TradePayload {
    double price{0.0};
    double quantity{0.0};
    std::string side;
};

using DecodedPayload = std::variant<std::monostate, SnapshotPayload, DeltaPayload, TradePayload>;

struct DecodedMessage {
    DecodedMessageType type{DecodedMessageType::kUnknown};
    std::string market_ticker;
    std::optional<std::uint64_t> seq_id;
    DecodedPayload data;
    std::string raw_payload;
};

} // namespace trading::shards
