#include "trading/shards/book_store.hpp"

namespace trading::shards {

bool BookStore::apply(const internal::NormalizedEvent& event) {
    if (event.market_ticker.empty() || event.type == internal::EventType::kUnknown) {
        return false;
    }

    auto [it, inserted] = books_.try_emplace(event.market_ticker);
    if (inserted) {
        it->second.market_ticker = event.market_ticker;
    }

    if (event.raw_sequence_id.has_value()) {
        it->second.last_seq_id = event.raw_sequence_id;
    } else if (event.meta.sequence_id != 0U) {
        it->second.last_seq_id = event.meta.sequence_id;
    }

    switch (event.type) {
    case internal::EventType::kSnapshot:
        ++it->second.snapshot_count;
        break;
    case internal::EventType::kDelta:
        ++it->second.delta_count;
        break;
    case internal::EventType::kTrade:
        ++it->second.trade_count;
        break;
    case internal::EventType::kHeartbeat:
    case internal::EventType::kStatus:
    case internal::EventType::kUnknown:
        return false;
    }
    return true;
}

const BookState* BookStore::find(std::string_view market_ticker) const {
    const auto book_it = books_.find(std::string(market_ticker));
    if (book_it == books_.end()) {
        return nullptr;
    }
    return &book_it->second;
}

std::size_t BookStore::size() const { return books_.size(); }

} // namespace trading::shards
