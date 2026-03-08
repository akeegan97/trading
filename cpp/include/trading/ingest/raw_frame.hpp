#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace trading::ingest {

struct RawFrame {
    std::chrono::steady_clock::time_point recv_timestamp;
    std::string source;
    std::string payload;
    std::string market_ticker;
    std::optional<std::uint64_t> seq_id;
};

} // namespace trading::ingest
