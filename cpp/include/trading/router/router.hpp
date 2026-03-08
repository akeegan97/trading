#pragma once

#include <cstddef>
#include <string_view>

#include "trading/decode/dispatch.hpp"
#include "trading/ingest/raw_frame.hpp"
#include "trading/router/shard_dispatch.hpp"

namespace trading::router {

class Router {
  public:
    Router(decode::DecodeFn decode_fn, IShardDispatch& shard_dispatch, std::size_t shard_count);

    [[nodiscard]] bool route(const ingest::RawFrame& frame);

  private:
    [[nodiscard]] static std::size_t compute_shard_id(std::string_view market_ticker,
                                                      std::size_t shard_count);

    decode::DecodeFn decode_fn_;
    IShardDispatch& shard_dispatch_;
    std::size_t shard_count_;
};

} // namespace trading::router
