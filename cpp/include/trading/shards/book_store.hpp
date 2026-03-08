#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "trading/shards/decoded_message.hpp"

namespace trading::shards {

struct BookState {
    std::string market_ticker;
    std::optional<std::uint64_t> last_seq_id;
    std::uint64_t snapshot_count{0};
    std::uint64_t delta_count{0};
    std::uint64_t trade_count{0};
};

class BookStore {
  public:
    [[nodiscard]] bool apply(const DecodedMessage& message);
    [[nodiscard]] const BookState* find(std::string_view market_ticker) const;
    [[nodiscard]] std::size_t size() const;

  private:
    std::unordered_map<std::string, BookState> books_;
};

} // namespace trading::shards
