#include "trading/router/router.hpp"

#include <functional>

#include "trading/model/normalized_event.hpp"
#include "trading/router/route_key.hpp"

namespace trading::router {
namespace {

constexpr std::size_t kMinShardCount = 1;

} // namespace

Router::Router(decode::DecodeFn decode_fn, IShardDispatch& shard_dispatch, std::size_t shard_count)
    : decode_fn_(decode_fn != nullptr ? decode_fn
                                      : decode::resolve_decoder(decode::ExchangeId::kKalshi)),
      shard_dispatch_(shard_dispatch),
      shard_count_(shard_count == 0 ? kMinShardCount : shard_count) {}

bool Router::route(const ingest::RawFrame& frame) {
    const decode::ExtractedFields fields = decode_fn_(frame);
    if (!fields.ok()) {
        return false;
    }

    const RouteKey route_key{
        .market_ticker = fields.market_ticker,
        .shard_id = compute_shard_id(fields.market_ticker, shard_count_),
    };
    const model::NormalizedEvent event{
        .recv_timestamp = fields.recv_timestamp,
        .source = fields.source,
        .market_ticker = fields.market_ticker,
        .seq_id = fields.seq_id,
        .raw_payload = frame.payload,
    };
    return shard_dispatch_.dispatch(route_key, event);
}

std::size_t Router::compute_shard_id(std::string_view market_ticker, std::size_t shard_count) {
    if (market_ticker.empty()) {
        return 0;
    }

    return std::hash<std::string_view>{}(market_ticker) % shard_count;
}

} // namespace trading::router
