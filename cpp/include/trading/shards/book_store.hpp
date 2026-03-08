#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "trading/internal/normalized_event.hpp"

namespace trading::shards {

struct BookState {
    using BidLevels = std::map<internal::PriceTicks, internal::QtyLots, std::greater<>>;
    using AskLevels = std::map<internal::PriceTicks, internal::QtyLots, std::less<>>;

    std::string market_ticker;
    std::optional<internal::SequenceId> last_seq_id;
    BidLevels bids;
    AskLevels asks;
    std::optional<internal::TradeData> last_trade;
    std::uint64_t snapshot_count{0};
    std::uint64_t delta_count{0};
    std::uint64_t trade_count{0};
    std::uint64_t stale_sequence_count{0};
    std::uint64_t apply_reject_count{0};
};

class BookStore {
  public:
    [[nodiscard]] bool apply(const internal::NormalizedEvent& event);
    [[nodiscard]] const BookState* find(std::string_view market_ticker) const;
    [[nodiscard]] std::size_t size() const;

  private:
    std::unordered_map<std::string, BookState> books_;
};

} // namespace trading::shards
