#include "trading/parsers/exchanges/polymarket/parser.hpp"

namespace trading::parsers::exchanges::polymarket {

ParseResult<internal::NormalizedEvent> Parser::parse(const internal::RouterFrame& frame) const {
    // TODO(polymarket): implement SIMDJSON on-demand parsing against Polymarket WS schema.
    // Expected shape mirrors Kalshi parser:
    // 1) classify message type (snapshot/delta/trade/status)
    // 2) map exchange fields -> internal::NormalizedEvent meta/data
    // 3) return ParseError::kMissingField / kInvalidField on malformed payloads
    (void)frame;
    (void)strict_field_validation_;
    return ParseResult<internal::NormalizedEvent>::failure(ParseError::kUnsupportedMessageType);
}

} // namespace trading::parsers::exchanges::polymarket
