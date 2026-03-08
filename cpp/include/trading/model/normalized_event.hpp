#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace trading::model {

struct NormalizedEvent {
    std::chrono::steady_clock::time_point recv_timestamp;
    std::string source;
    std::string market_ticker;
    std::optional<std::uint64_t> seq_id;
    std::string raw_payload;
};

} // namespace trading::model
