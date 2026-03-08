#include "trading/shards/message_parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace trading::shards {
namespace {

using Json = nlohmann::json;

std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

const Json* object_field(const Json& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto key_it = object.find(std::string(key));
    if (key_it == object.end()) {
        return nullptr;
    }
    return &(*key_it);
}

std::vector<const Json*> make_search_roots(const Json& root) {
    std::vector<const Json*> roots;
    roots.push_back(&root);
    for (const std::string_view key : {"data", "params", "msg"}) {
        const Json* child = object_field(root, key);
        if (child != nullptr && child->is_object()) {
            roots.push_back(child);
        }
    }
    return roots;
}

const Json* find_in_roots(const std::vector<const Json*>& roots,
                          std::initializer_list<std::string_view> keys) {
    for (const Json* root : roots) {
        for (std::string_view key : keys) {
            const Json* value = object_field(*root, key);
            if (value != nullptr) {
                return value;
            }
        }
    }
    return nullptr;
}

std::optional<std::string> json_to_string(const Json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer() || value.is_number_unsigned() || value.is_number_float()) {
        return value.dump();
    }
    return std::nullopt;
}

std::optional<double> json_to_double(const Json& value) {
    if (value.is_number_float()) {
        return value.get<double>();
    }
    if (value.is_number_integer()) {
        return static_cast<double>(value.get<std::int64_t>());
    }
    if (value.is_number_unsigned()) {
        return static_cast<double>(value.get<std::uint64_t>());
    }
    if (value.is_string()) {
        const auto& as_string = value.get_ref<const std::string&>();
        try {
            return std::stod(as_string);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> json_to_uint64(const Json& value) {
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        const auto parsed = value.get<std::int64_t>();
        if (parsed < 0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(parsed);
    }
    if (value.is_string()) {
        const auto& as_string = value.get_ref<const std::string&>();
        try {
            return static_cast<std::uint64_t>(std::stoull(as_string));
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> extract_string(const std::vector<const Json*>& roots,
                                          std::initializer_list<std::string_view> keys) {
    const Json* value = find_in_roots(roots, keys);
    if (value == nullptr) {
        return std::nullopt;
    }
    return json_to_string(*value);
}

std::optional<std::uint64_t> extract_uint64(const std::vector<const Json*>& roots,
                                            std::initializer_list<std::string_view> keys) {
    const Json* value = find_in_roots(roots, keys);
    if (value == nullptr) {
        return std::nullopt;
    }
    return json_to_uint64(*value);
}

DecodedMessageType classify_message_type(const Json& parsed_root,
                                         const std::vector<const Json*>& roots,
                                         std::string_view raw_payload) {
    std::optional<std::string> message_type =
        extract_string(roots, {"type", "event", "msg_type", "message_type"});
    if (!message_type.has_value()) {
        const Json* type_in_data = object_field(parsed_root, "data");
        if (type_in_data != nullptr && type_in_data->is_object()) {
            message_type = extract_string({type_in_data}, {"type", "event", "msg_type"});
        }
    }

    const std::string normalized_type =
        message_type.has_value() ? to_lower(*message_type) : to_lower(std::string(raw_payload));
    if (normalized_type.find("snapshot") != std::string::npos) {
        return DecodedMessageType::kSnapshot;
    }
    if (normalized_type.find("delta") != std::string::npos ||
        normalized_type.find("update") != std::string::npos) {
        return DecodedMessageType::kDelta;
    }
    if (normalized_type.find("trade") != std::string::npos) {
        return DecodedMessageType::kTrade;
    }
    return DecodedMessageType::kUnknown;
}

std::optional<PriceLevel> parse_level(const Json& level_json) {
    if (level_json.is_array() && level_json.size() >= 2) {
        std::optional<double> price = json_to_double(level_json[0]);
        std::optional<double> quantity = json_to_double(level_json[1]);
        if (price.has_value() && quantity.has_value()) {
            return PriceLevel{
                .price = *price,
                .quantity = *quantity,
            };
        }
        return std::nullopt;
    }

    if (!level_json.is_object()) {
        return std::nullopt;
    }

    const Json* price_json = object_field(level_json, "price");
    if (price_json == nullptr) {
        price_json = object_field(level_json, "px");
    }
    const Json* quantity_json = object_field(level_json, "size");
    if (quantity_json == nullptr) {
        quantity_json = object_field(level_json, "quantity");
    }
    if (quantity_json == nullptr) {
        quantity_json = object_field(level_json, "qty");
    }

    if (price_json == nullptr || quantity_json == nullptr) {
        return std::nullopt;
    }

    std::optional<double> price = json_to_double(*price_json);
    std::optional<double> quantity = json_to_double(*quantity_json);
    if (!price.has_value() || !quantity.has_value()) {
        return std::nullopt;
    }

    return PriceLevel{
        .price = *price,
        .quantity = *quantity,
    };
}

std::vector<PriceLevel> parse_side_levels(const Json* side_json) {
    if (side_json == nullptr || !side_json->is_array()) {
        return {};
    }

    std::vector<PriceLevel> levels;
    levels.reserve(side_json->size());
    for (const auto& level_json : *side_json) {
        std::optional<PriceLevel> level = parse_level(level_json);
        if (level.has_value()) {
            levels.push_back(*level);
        }
    }
    return levels;
}

std::optional<DecodedPayload> parse_snapshot_or_delta(const std::vector<const Json*>& roots,
                                                      bool is_snapshot) {
    const Json* bids_json = find_in_roots(roots, {"bids", "bid"});
    const Json* asks_json = find_in_roots(roots, {"asks", "ask"});

    std::vector<PriceLevel> bids = parse_side_levels(bids_json);
    std::vector<PriceLevel> asks = parse_side_levels(asks_json);
    if (bids.empty() && asks.empty()) {
        return std::nullopt;
    }

    if (is_snapshot) {
        return SnapshotPayload{
            .bids = std::move(bids),
            .asks = std::move(asks),
        };
    }
    return DeltaPayload{
        .bids = std::move(bids),
        .asks = std::move(asks),
    };
}

std::optional<DecodedPayload> parse_trade(const std::vector<const Json*>& roots) {
    const Json* price_json = find_in_roots(roots, {"price", "px"});
    const Json* quantity_json = find_in_roots(roots, {"size", "quantity", "qty", "amount"});
    if (price_json == nullptr || quantity_json == nullptr) {
        return std::nullopt;
    }

    std::optional<double> price = json_to_double(*price_json);
    std::optional<double> quantity = json_to_double(*quantity_json);
    if (!price.has_value() || !quantity.has_value()) {
        return std::nullopt;
    }

    std::string side;
    std::optional<std::string> parsed_side = extract_string(roots, {"side", "taker_side"});
    if (parsed_side.has_value()) {
        side = to_lower(*parsed_side);
    }

    return TradePayload{
        .price = *price,
        .quantity = *quantity,
        .side = std::move(side),
    };
}

std::optional<DecodedPayload> parse_payload(DecodedMessageType type,
                                            const std::vector<const Json*>& roots) {
    switch (type) {
    case DecodedMessageType::kSnapshot:
        return parse_snapshot_or_delta(roots, true);
    case DecodedMessageType::kDelta:
        return parse_snapshot_or_delta(roots, false);
    case DecodedMessageType::kTrade:
        return parse_trade(roots);
    case DecodedMessageType::kUnknown:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::optional<DecodedMessage> HeuristicMessageParser::parse(
    const router::RoutedEvent& routed_event) const {
    const Json parsed_root = Json::parse(routed_event.event.raw_payload, nullptr, false);
    if (parsed_root.is_discarded()) {
        return std::nullopt;
    }

    const auto roots = make_search_roots(parsed_root);
    const DecodedMessageType type =
        classify_message_type(parsed_root, roots, routed_event.event.raw_payload);
    if (type == DecodedMessageType::kUnknown) {
        return std::nullopt;
    }

    std::string market_ticker = routed_event.event.market_ticker;
    std::optional<std::string> parsed_ticker =
        extract_string(roots, {"market_ticker", "ticker", "marketTicker"});
    if (parsed_ticker.has_value() && !parsed_ticker->empty()) {
        market_ticker = *parsed_ticker;
    }
    if (market_ticker.empty()) {
        return std::nullopt;
    }

    std::optional<std::uint64_t> seq_id = routed_event.event.seq_id;
    std::optional<std::uint64_t> parsed_seq =
        extract_uint64(roots, {"seq", "seq_id", "sequence", "sequence_id"});
    if (parsed_seq.has_value()) {
        seq_id = parsed_seq;
    }

    std::optional<DecodedPayload> parsed_payload = parse_payload(type, roots);
    if (!parsed_payload.has_value()) {
        return std::nullopt;
    }

    return DecodedMessage{
        .type = type,
        .market_ticker = std::move(market_ticker),
        .seq_id = seq_id,
        .data = std::move(*parsed_payload),
        .raw_payload = routed_event.event.raw_payload,
    };
}

} // namespace trading::shards
