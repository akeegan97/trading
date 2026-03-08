#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>
#include <vector>

#include "support/fake_ws_transport.hpp"
#include "trading/adapters/exchanges/kalshi/auth_signer.hpp"
#include "trading/adapters/exchanges/kalshi/ws_adapter.hpp"
#include "trading/adapters/ws/session.hpp"
#include "trading/decode/extracted_fields.hpp"
#include "trading/engine/runtime.hpp"
#include "trading/ingest/raw_frame.hpp"
#include "trading/internal/normalized_event.hpp"
#include "trading/internal/router_frame.hpp"
#include "trading/parsers/exchanges/kalshi/parser.hpp"
#include "trading/router/router.hpp"
#include "trading/router/shard_dispatch.hpp"
#include "trading/shards/message_parser.hpp"

namespace {

constexpr const char* kTestPrivateKeyPem = R"(-----BEGIN PRIVATE KEY-----
MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQDC0wMNDKatICXc
VHJGfoGoMmz8BoF7HwhTl7CCXAZ55lLvCaLY2LhZumiWRuIHaHZDl2DhZMCDNJ3F
eIEawGjpjlWsj7WGXzSsTZUQLS0TVq5Jr5AtFWbvFn/cYySD47/aFB3mqgUPDS7G
0tTx/RpHjsYdp4j/aES5lBArQVzWVjd8zlKP90Q2J39H2soahvUUNW6hxhz2XFPq
N6YgKclwSApt9177ShO6fPQ7wa4RL+1rRbLPTkZKbW4S2JF8xZ1/RYUFZNKwG1WX
Hp4ZY8wIDReDIN9/ksh33DbVGloIjbJfRo8dMqawwIw03ZJol8KbQIGOlxlLEbdP
H4mgFv7ZAgMBAAECggEAA45NQpzq7lTFOH8/Qpz8tw/jpJFPAzDLMfPvqBkVexXG
5fU7RXdaP6kwCXMKV3z0ROgkVxmJpBE8E9xO5mL7sWav/3zocmFKsh5MebihujvK
iD/93SXhSPb9GHyBB65n3QtP/zfZPnhsyO8sClBg1HsdMqB/tAI7Y8tFGUeOmr9O
WOY359vG5xJlCBhhQ1hWaxVn1zb+Bs3Cs4dqpFnRpJP0xUuMKi6TDXHhUnWsw6Rr
cSC3zeLJ/2lDO58X9T5GEuIjvIZjkDNrvkV3OgLiHAt6R2b+scXirZHzYwAd1s3o
W1vmLPkAAXIip2IYndtIBjMEqMp5+6dHaXel83H0FQKBgQDqesT746NvBwDpSmZj
ho6XiyTQKS6SERK3I3hSKRAXgdOfc5OLWCaHpgitnhaCQ4Zl4lusnuBNFkJnLUZ3
v2CTjrGqUCZtOAy+5k2Fr+HwmmDGCqYXfN6xbZfH0y+kzk8lDZAyT/VkMv3gmtTE
A4FQKVb870xra5J5jwtxEerD1QKBgQDUtITDCtr1aWW6dUF8JA5S3u3HiC2XOdTZ
h5dzLxHWCPtIJrTSYINrx0kqvr6uefTOlWel5ckJ8l/qwPtEyZ36SaQvvrxhew5/
monY3kKLisBQLFyfdwAgKjOTBso3Ydfi0OL5GfnVgRdgPSt96OarLHf/Oj252Ef0
/B9AiFFE9QKBgELrZuy8uxgk1429PhuJe4iknY0LM89cMAs0hFJuhbkvxEXtpl5O
ejCcoj1qsOHcg67wO1m4vitB/xGTSllCtT1nrCz/Uvg41upksBtHZyRT6uqaf2yU
prncnUTacO6IMu5QQKcbSt2d7/b6OV734SAPrFPjt9uwh81JlepfQrFBAoGAYrD/
iJ/YyFWG5eTR5Y+1Na5KFXKR9MDYibXoB4GnfO/d57RN4e72C2QEBtlKEBp2BAQ+
ezMK/JqA9qNj8p65tg/FJoIRVgmKyJojq4Q0zBos8QYkU7kbTdFG7MkRunCjgpH5
PitXSEEFX5oCPAF8RZ/0bW/QhBJeEMWKmp6lVqECgYBQOlGbPcpEIDo738T3yADv
dKLBmz0Z/Rgg22dVhjhhUKYTzjc8spVyUFmXjm9/RFrqZRBZ+r52ZZS9mO/D50PR
Hs7GYP2lYjn+hqDzxRLUDyDFxdKcxVtz4ye9iTCiaOFdCBRDQOMiViyG3bzaEWqv
wMev4lw7N9lCKEbZCmutaA==
-----END PRIVATE KEY-----)";

trading::adapters::exchanges::kalshi::WsAdapter make_kalshi_adapter() {
    return trading::adapters::exchanges::kalshi::WsAdapter{
        trading::adapters::exchanges::kalshi::AuthSigner{
            trading::adapters::exchanges::kalshi::Credentials{
                .key_id = "test-key-id",
                .private_key_pem = kTestPrivateKeyPem,
            },
        },
    };
}

trading::decode::ExtractedFields passthrough_decode(const trading::ingest::RawFrame& frame) {
    return trading::decode::ExtractedFields{
        .status = trading::decode::DecodeStatus::kOk,
        .market_ticker = frame.market_ticker,
        .seq_id = frame.seq_id,
        .recv_timestamp = frame.recv_timestamp,
        .source = frame.source,
    };
}

trading::ingest::RawFrame make_raw_frame(std::string ticker,
                                         std::string payload,
                                         std::uint64_t seq_id) {
    trading::ingest::RawFrame frame{};
    frame.recv_timestamp = std::chrono::steady_clock::now();
    frame.source = "kalshi";
    frame.payload = std::move(payload);
    frame.market_ticker = std::move(ticker);
    frame.seq_id = seq_id;
    return frame;
}

std::vector<trading::router::RoutedEvent> drain_all_shards(
    trading::router::ShardedEventDispatch& shard_dispatch) {
    std::vector<trading::router::RoutedEvent> drained;
    for (std::size_t shard_id = 0; shard_id < shard_dispatch.shard_count(); ++shard_id) {
        trading::router::RoutedEvent routed{};
        while (shard_dispatch.try_pop(shard_id, routed)) {
            drained.push_back(routed);
        }
    }
    return drained;
}

trading::router::RoutedEvent make_routed_event(std::string raw_payload,
                                               std::string market_ticker,
                                               std::optional<std::uint64_t> seq_id) {
    return trading::router::RoutedEvent{
        .route_key =
            {
                .market_ticker = market_ticker,
                .shard_id = 0,
            },
        .event =
            {
                .recv_timestamp = std::chrono::steady_clock::now(),
                .source = "kalshi",
                .market_ticker = std::move(market_ticker),
                .seq_id = seq_id,
                .raw_payload = std::move(raw_payload),
            },
    };
}

std::optional<std::size_t> common_shard_id(
    const std::vector<trading::router::RoutedEvent>& routed_events) {
    if (routed_events.empty()) {
        return std::nullopt;
    }

    const std::size_t first_shard_id = routed_events.front().route_key.shard_id;
    for (const auto& routed : routed_events) {
        if (routed.route_key.shard_id != first_shard_id) {
            return std::nullopt;
        }
    }
    return first_shard_id;
}

const trading::shards::DecodedMessage& require_decoded(
    const std::optional<trading::shards::DecodedMessage>& decoded) {
    EXPECT_TRUE(decoded.has_value());
    static const trading::shards::DecodedMessage kEmpty{};
    if (!decoded.has_value()) {
        return kEmpty;
    }
    return decoded.value();
}

void expect_all_market_tickers(const std::vector<trading::router::RoutedEvent>& routed_events,
                               std::string_view expected_market_ticker) {
    for (const auto& routed : routed_events) {
        EXPECT_EQ(routed.event.market_ticker, expected_market_ticker);
    }
}

const trading::internal::NormalizedEvent& require_parsed(
    const trading::parsers::ParseResult<trading::internal::NormalizedEvent>& parsed_result) {
    EXPECT_TRUE(parsed_result.ok()) << " parse_error=" << static_cast<int>(parsed_result.error());
    static const trading::internal::NormalizedEvent kEmpty{};
    if (!parsed_result.ok()) {
        return kEmpty;
    }
    return parsed_result.value().value();
}

TEST(TradingCoreTest, StartupPayloadHasExpectedFields) {
    const auto startup = trading::engine::build_trader_startup_payload("test");
    const auto startup_json = nlohmann::json::parse(startup);

    EXPECT_EQ(startup_json.at("service"), "trading_engine");
    EXPECT_EQ(startup_json.at("status"), "ok");
    EXPECT_EQ(startup_json.at("mode"), "test");
}

TEST(WsSessionTest, ConnectBuildsKalshiHeaders) {
    const auto kalshi_adapter = make_kalshi_adapter();
    trading::tests::FakeWsTransport transport;
    trading::adapters::ws::WsSession session{transport, kalshi_adapter};

    ASSERT_TRUE(session.connect());
    const auto& connect_request = session.connect_request();

    EXPECT_FALSE(connect_request.endpoint.empty());
    EXPECT_TRUE(connect_request.headers.contains("KALSHI-ACCESS-KEY"));
    EXPECT_TRUE(connect_request.headers.contains("KALSHI-ACCESS-SIGNATURE"));
    EXPECT_TRUE(connect_request.headers.contains("KALSHI-ACCESS-TIMESTAMP"));
    EXPECT_FALSE(connect_request.headers.at("KALSHI-ACCESS-SIGNATURE").empty());
    EXPECT_FALSE(connect_request.headers.at("KALSHI-ACCESS-TIMESTAMP").empty());
    EXPECT_TRUE(transport.connected());
    EXPECT_EQ(transport.config().endpoint, connect_request.endpoint);
}

TEST(WsSessionTest, SubscribeSendsExpectedPayload) {
    const auto kalshi_adapter = make_kalshi_adapter();
    trading::tests::FakeWsTransport transport;
    trading::adapters::ws::WsSession session{transport, kalshi_adapter};

    ASSERT_TRUE(session.connect());
    ASSERT_TRUE(session.subscribe("trades"));
    ASSERT_EQ(transport.sent_messages().size(), 1U);

    const auto subscribe_json = nlohmann::json::parse(session.last_subscribe_payload());
    EXPECT_EQ(subscribe_json.at("cmd"), "subscribe");
    EXPECT_EQ(subscribe_json.at("params").at("channels").at(0), "trades");
}

TEST(RouterTest, RoutesEventIntoExactlyOneShardQueue) {
    constexpr std::size_t kShardCount = 4;
    constexpr std::size_t kPerShardQueueCapacity = 64;
    constexpr std::uint64_t kFirstSeqId = 7;

    trading::router::ShardedEventDispatch shard_dispatch{
        trading::router::ShardedEventDispatchConfig{
            .shard_count = kShardCount,
            .per_shard_queue_capacity = kPerShardQueueCapacity,
        },
    };
    trading::router::Router router{passthrough_decode, shard_dispatch, kShardCount};

    const trading::ingest::RawFrame frame =
        make_raw_frame("KXBTC-YES", R"({"type":"trade","id":1})", kFirstSeqId);

    ASSERT_TRUE(router.route(frame));

    const auto routed_events = drain_all_shards(shard_dispatch);
    ASSERT_EQ(routed_events.size(), 1U);
    EXPECT_EQ(routed_events[0].event.market_ticker, frame.market_ticker);
    ASSERT_TRUE(routed_events[0].event.seq_id.has_value());
    EXPECT_EQ(routed_events[0].event.seq_id.value_or(0U), kFirstSeqId);
    EXPECT_LT(routed_events[0].route_key.shard_id, kShardCount);
    EXPECT_EQ(shard_dispatch.dropped_count(), 0U);
}

TEST(RouterTest, RoutesSameMarketToSameShard) {
    constexpr std::size_t kShardCount = 8;
    constexpr std::size_t kPerShardQueueCapacity = 64;
    constexpr std::uint64_t kFirstSeqId = 11;
    constexpr std::uint64_t kSecondSeqId = 12;
    constexpr std::size_t kExpectedEvents = 2;

    trading::router::ShardedEventDispatch shard_dispatch{
        trading::router::ShardedEventDispatchConfig{
            .shard_count = kShardCount,
            .per_shard_queue_capacity = kPerShardQueueCapacity,
        },
    };
    trading::router::Router router{passthrough_decode, shard_dispatch, kShardCount};

    const trading::ingest::RawFrame first =
        make_raw_frame("KXBTC-YES", R"({"type":"trade","id":1})", kFirstSeqId);
    const trading::ingest::RawFrame second =
        make_raw_frame("KXBTC-YES", R"({"type":"trade","id":2})", kSecondSeqId);

    ASSERT_TRUE(router.route(first));
    ASSERT_TRUE(router.route(second));

    const auto routed_events = drain_all_shards(shard_dispatch);
    ASSERT_EQ(routed_events.size(), kExpectedEvents);

    const auto seen_shard_id = common_shard_id(routed_events);
    ASSERT_TRUE(seen_shard_id.has_value());
    expect_all_market_tickers(routed_events, first.market_ticker);
    EXPECT_EQ(shard_dispatch.dropped_count(), 0U);
}

TEST(MessageParserTest, ParsesSnapshotPayloadIntoStructuredLevels) {
    constexpr std::uint64_t kExpectedSeqId = 42;
    const auto routed_event =
        make_routed_event(R"({
            "type":"snapshot",
            "market_ticker":"KXBTC-YES",
            "seq":42,
            "bids":[[100.5,3],[100.0,2]],
            "asks":[[101.0,1]]
        })",
                          "ignored",
                          std::nullopt);
    const trading::shards::HeuristicMessageParser parser;
    const auto decoded = parser.parse(routed_event);

    const auto& parsed = require_decoded(decoded);
    EXPECT_EQ(parsed.type, trading::shards::DecodedMessageType::kSnapshot);
    EXPECT_EQ(parsed.market_ticker, "KXBTC-YES");
    EXPECT_EQ(parsed.seq_id.value_or(0U), kExpectedSeqId);

    const auto* snapshot = std::get_if<trading::shards::SnapshotPayload>(&parsed.data);
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snapshot->bids.size(), 2U);
    ASSERT_EQ(snapshot->asks.size(), 1U);
    EXPECT_DOUBLE_EQ(snapshot->bids[0].price, 100.5);
    EXPECT_DOUBLE_EQ(snapshot->bids[0].quantity, 3.0);
}

TEST(MessageParserTest, ParsesTradePayloadWithNestedDataObject) {
    constexpr std::uint64_t kExpectedSeqId = 77;
    const auto routed_event = make_routed_event(
        R"({
            "type":"trade",
            "data":{
              "market_ticker":"KXBTC-YES",
              "seq_id":"77",
              "price":"101.25",
              "size":"4",
              "side":"YES"
            }
        })",
        "",
        std::nullopt);
    const trading::shards::HeuristicMessageParser parser;
    const auto decoded = parser.parse(routed_event);

    const auto& parsed = require_decoded(decoded);
    EXPECT_EQ(parsed.type, trading::shards::DecodedMessageType::kTrade);
    EXPECT_EQ(parsed.market_ticker, "KXBTC-YES");
    EXPECT_EQ(parsed.seq_id.value_or(0U), kExpectedSeqId);

    const auto* trade = std::get_if<trading::shards::TradePayload>(&parsed.data);
    ASSERT_NE(trade, nullptr);
    EXPECT_DOUBLE_EQ(trade->price, 101.25);
    EXPECT_DOUBLE_EQ(trade->quantity, 4.0);
    EXPECT_EQ(trade->side, "yes");
}

TEST(MessageParserTest, FallsBackToRoutedMetadataWhenJsonFieldsMissing) {
    constexpr std::uint64_t kExpectedSeqId = 555;
    const auto routed_event = make_routed_event(
        R"({
            "type":"delta",
            "bids":[{"price":100.0,"size":1.0}]
        })",
        "KXBTC-YES",
        kExpectedSeqId);
    const trading::shards::HeuristicMessageParser parser;
    const auto decoded = parser.parse(routed_event);

    const auto& parsed = require_decoded(decoded);
    EXPECT_EQ(parsed.type, trading::shards::DecodedMessageType::kDelta);
    EXPECT_EQ(parsed.market_ticker, "KXBTC-YES");
    EXPECT_EQ(parsed.seq_id.value_or(0U), kExpectedSeqId);

    const auto* delta = std::get_if<trading::shards::DeltaPayload>(&parsed.data);
    ASSERT_NE(delta, nullptr);
    ASSERT_EQ(delta->bids.size(), 1U);
    EXPECT_DOUBLE_EQ(delta->bids[0].price, 100.0);
}

TEST(MessageParserTest, RejectsInvalidJsonPayload) {
    const auto routed_event = make_routed_event("{ not-json", "KXBTC-YES", 1);
    const trading::shards::HeuristicMessageParser parser;
    EXPECT_FALSE(parser.parse(routed_event).has_value());
}

TEST(KalshiParserTest, ParsesOrderbookSnapshotDocsExample) {
    constexpr std::string_view kTicker = "FED-23DEC-T3.00";
    constexpr std::string_view kPayload = R"({
        "type": "orderbook_snapshot",
        "sid": 3,
        "seq": 2,
        "msg": {
            "market_ticker": "FED-23DEC-T3.00",
            "yes": [[95, 54], [94, 1]],
            "no": [[7, 87], [6, 32]]
        }
    })";

    const trading::parsers::exchanges::kalshi::Parser parser;
    const trading::internal::RouterFrame frame{
        .exchange = trading::internal::ExchangeId::kKalshi,
        .market_ticker = std::string{kTicker},
        .sequence_id = std::nullopt,
        .recv_ns = 17U,
        .raw_payload_view = kPayload,
    };
    const auto parsed_result = parser.parse(frame);
    const auto& event = require_parsed(parsed_result);

    EXPECT_EQ(event.type, trading::internal::EventType::kSnapshot);
    EXPECT_EQ(event.meta.exchange, trading::internal::ExchangeId::kKalshi);
    ASSERT_TRUE(event.raw_sequence_id.has_value());
    EXPECT_EQ(event.raw_sequence_id.value(), 2U);
    EXPECT_EQ(event.meta.sequence_id, 2U);

    const auto* snapshot = std::get_if<trading::internal::SnapshotData>(&event.data);
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snapshot->bids.size(), 2U);
    ASSERT_EQ(snapshot->asks.size(), 2U);
    EXPECT_EQ(snapshot->bids[0].price_ticks, 95);
    EXPECT_EQ(snapshot->bids[0].qty_lots, 54);
    EXPECT_EQ(snapshot->asks[0].price_ticks, 7);
    EXPECT_EQ(snapshot->asks[0].qty_lots, 87);
}

TEST(KalshiParserTest, ParsesOrderbookDeltaDocsExample) {
    constexpr std::string_view kTicker = "FED-23DEC-T3.00";
    constexpr std::string_view kPayload = R"({
        "type": "orderbook_delta",
        "sid": 3,
        "seq": 3,
        "msg": {
            "market_ticker": "FED-23DEC-T3.00",
            "price": 95,
            "delta": -54,
            "side": "yes"
        }
    })";

    const trading::parsers::exchanges::kalshi::Parser parser;
    const trading::internal::RouterFrame frame{
        .exchange = trading::internal::ExchangeId::kKalshi,
        .market_ticker = std::string{kTicker},
        .sequence_id = std::nullopt,
        .recv_ns = 19U,
        .raw_payload_view = kPayload,
    };
    const auto parsed_result = parser.parse(frame);
    const auto& event = require_parsed(parsed_result);

    EXPECT_EQ(event.type, trading::internal::EventType::kDelta);
    ASSERT_TRUE(event.raw_sequence_id.has_value());
    EXPECT_EQ(event.raw_sequence_id.value(), 3U);
    EXPECT_EQ(event.meta.sequence_id, 3U);

    const auto* delta = std::get_if<trading::internal::DeltaData>(&event.data);
    ASSERT_NE(delta, nullptr);
    EXPECT_EQ(delta->side, trading::internal::Side::kBid);
    EXPECT_EQ(delta->price_ticks, 95);
    EXPECT_EQ(delta->delta_qty_lots, -54);
}

TEST(KalshiParserTest, ParsesTradeDocsExample) {
    constexpr std::string_view kTicker = "HIGHNY-25MAR11-B57.25";
    constexpr std::string_view kPayload = R"({
        "type": "trade",
        "msg": {
            "market_ticker": "HIGHNY-25MAR11-B57.25",
            "yes_price": 17,
            "no_price": 84,
            "count": 17,
            "taker_side": "yes",
            "ts": 1741098958832
        }
    })";

    const trading::parsers::exchanges::kalshi::Parser parser;
    const trading::internal::RouterFrame frame{
        .exchange = trading::internal::ExchangeId::kKalshi,
        .market_ticker = std::string{kTicker},
        .sequence_id = std::nullopt,
        .recv_ns = 23U,
        .raw_payload_view = kPayload,
    };
    const auto parsed_result = parser.parse(frame);
    const auto& event = require_parsed(parsed_result);

    EXPECT_EQ(event.type, trading::internal::EventType::kTrade);
    EXPECT_FALSE(event.raw_sequence_id.has_value());

    const auto* trade = std::get_if<trading::internal::TradeData>(&event.data);
    ASSERT_NE(trade, nullptr);
    EXPECT_EQ(trade->price_ticks, 17);
    EXPECT_EQ(trade->qty_lots, 17);
    EXPECT_EQ(trade->aggressor, trading::internal::Side::kBuy);
}

} // namespace
