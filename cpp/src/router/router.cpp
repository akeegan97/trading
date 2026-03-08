#include "trading/router/router.hpp"

#include <chrono>
#include <functional>

#include "trading/router/route_key.hpp"

namespace trading::router {
namespace {

constexpr std::size_t kMinShardCount = 1;

} // namespace

Router::Router(decode::DecodeFn decode_fn, IShardDispatch& shard_dispatch, std::size_t shard_count,
               internal::ExchangeId exchange_id)
    : decode_fn_(decode_fn != nullptr ? decode_fn
                                      : decode::resolve_decoder(decode::ExchangeId::kKalshi)),
      shard_dispatch_(shard_dispatch),
      shard_count_(shard_count == 0 ? kMinShardCount : shard_count), exchange_id_(exchange_id) {}

bool Router::route(const ingest::RawFrame& frame) {
    const decode::ExtractedFields fields = decode_fn_(frame);
    if (!fields.ok()) {
        return false;
    }

    const RouteKey route_key{
        .market_ticker = fields.market_ticker,
        .shard_id = compute_shard_id(fields.market_ticker, shard_count_),
    };
    const auto recv_count = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                fields.recv_timestamp.time_since_epoch())
                                .count();
    const RoutedFrame routed_frame{
        .exchange = exchange_id_,
        .market_ticker = fields.market_ticker,
        .sequence_id = fields.seq_id,
        .recv_ns = recv_count > 0 ? static_cast<internal::TimestampNs>(recv_count) : 0U,
        .raw_payload = frame.payload,
    };
    return shard_dispatch_.dispatch(route_key, routed_frame);
}

std::size_t Router::compute_shard_id(std::string_view market_ticker, std::size_t shard_count) {
    if (market_ticker.empty()) {
        return 0;
    }

    return std::hash<std::string_view>{}(market_ticker) % shard_count;
}

} // namespace trading::router
