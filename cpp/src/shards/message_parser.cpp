#include "trading/shards/message_parser.hpp"

#include "trading/parsers/exchanges/kalshi/parser.hpp"
#include "trading/parsers/exchanges/polymarket/parser.hpp"

namespace trading::shards {

parsers::ParseResult<internal::NormalizedEvent>
RoutedEventParser::parse(const router::RoutedEvent& routed_event) const {
    const internal::RouterFrame frame{
        .exchange = routed_event.frame.exchange,
        .market_ticker = routed_event.frame.market_ticker,
        .sequence_id = routed_event.frame.sequence_id,
        .recv_ns = routed_event.frame.recv_ns,
        .raw_payload_view = routed_event.frame.raw_payload,
    };

    switch (routed_event.frame.exchange) {
    case internal::ExchangeId::kKalshi: {
        const parsers::exchanges::kalshi::Parser parser;
        return parser.parse(frame);
    }
    case internal::ExchangeId::kPolymarket: {
        const parsers::exchanges::polymarket::Parser parser;
        return parser.parse(frame);
    }
    case internal::ExchangeId::kUnknown:
        return parsers::ParseResult<internal::NormalizedEvent>::failure(
            parsers::ParseError::kUnsupportedMessageType);
    }

    return parsers::ParseResult<internal::NormalizedEvent>::failure(
        parsers::ParseError::kUnsupportedMessageType);
}

} // namespace trading::shards
