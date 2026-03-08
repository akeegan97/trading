#include "trading/shards/book_store.hpp"

namespace trading::shards {

bool BookStore::apply(const DecodedMessage& message) {
    if (message.market_ticker.empty() || message.type == DecodedMessageType::kUnknown) {
        return false;
    }

    auto [it, inserted] = books_.try_emplace(message.market_ticker);
    if (inserted) {
        it->second.market_ticker = message.market_ticker;
    }

    it->second.last_seq_id = message.seq_id;
    switch (message.type) {
    case DecodedMessageType::kSnapshot:
        ++it->second.snapshot_count;
        break;
    case DecodedMessageType::kDelta:
        ++it->second.delta_count;
        break;
    case DecodedMessageType::kTrade:
        ++it->second.trade_count;
        break;
    case DecodedMessageType::kUnknown:
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
