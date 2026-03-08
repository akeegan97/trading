#pragma once

#include <cstddef>
#include <string>

namespace trading::router {

struct RouteKey {
    std::string market_ticker;
    std::size_t shard_id{0};
};

} // namespace trading::router
