#include "trading/shards/book_store.hpp"

namespace trading::shards {
namespace {

bool update_sequence(BookState& book, const internal::NormalizedEvent& event) {
    const std::optional<internal::SequenceId> sequence_id = event.effective_sequence_id();
    if (!sequence_id.has_value()) {
        return true;
    }

    if (book.last_seq_id.has_value()) {
        const auto last_seq = book.last_seq_id.value();
        const auto next_seq = sequence_id.value();
        // Some feeds use connection-global sequences (e.g. interleaved across markets),
        // so books should only enforce strictly increasing order, not contiguous increments.
        if (next_seq <= last_seq) {
            ++book.stale_sequence_count;
            return false;
        }
    }

    book.last_seq_id = sequence_id;
    return true;
}

bool apply_snapshot_data(BookState& book, const internal::SnapshotData& snapshot) {
    book.bids.clear();
    book.asks.clear();

    for (const auto& level : snapshot.bids) {
        if (level.price_ticks < 0 || level.qty_lots <= 0) {
            return false;
        }
        book.bids[level.price_ticks] = level.qty_lots;
    }
    for (const auto& level : snapshot.asks) {
        if (level.price_ticks < 0 || level.qty_lots <= 0) {
            return false;
        }
        book.asks[level.price_ticks] = level.qty_lots;
    }
    return true;
}

bool apply_delta_data(BookState& book, const internal::DeltaData& delta) {
    if (delta.price_ticks < 0) {
        return false;
    }

    BookState::BidLevels* bids = &book.bids;
    BookState::AskLevels* asks = &book.asks;
    switch (delta.side) {
    case internal::Side::kBid:
    case internal::Side::kBuy: {
        const auto existing_it = bids->find(delta.price_ticks);
        const internal::QtyLots existing_qty = existing_it == bids->end() ? 0 : existing_it->second;
        const internal::QtyLots updated_qty = existing_qty + delta.delta_qty_lots;
        if (updated_qty <= 0) {
            bids->erase(delta.price_ticks);
        } else {
            (*bids)[delta.price_ticks] = updated_qty;
        }
        return true;
    }
    case internal::Side::kAsk:
    case internal::Side::kSell: {
        const auto existing_it = asks->find(delta.price_ticks);
        const internal::QtyLots existing_qty = existing_it == asks->end() ? 0 : existing_it->second;
        const internal::QtyLots updated_qty = existing_qty + delta.delta_qty_lots;
        if (updated_qty <= 0) {
            asks->erase(delta.price_ticks);
        } else {
            (*asks)[delta.price_ticks] = updated_qty;
        }
        return true;
    }
    case internal::Side::kUnknown:
        return false;
    }
    return false;
}

} // namespace

bool BookStore::apply(const internal::NormalizedEvent& event) {
    if (event.market_ticker.empty() || event.type == internal::EventType::kUnknown) {
        return false;
    }

    auto [it, inserted] = books_.try_emplace(event.market_ticker);
    if (inserted) {
        it->second.market_ticker = event.market_ticker;
    }
    if (!update_sequence(it->second, event)) {
        ++it->second.apply_reject_count;
        return false;
    }

    switch (event.type) {
    case internal::EventType::kSnapshot: {
        const auto* snapshot = std::get_if<internal::SnapshotData>(&event.data);
        if (snapshot == nullptr || !apply_snapshot_data(it->second, *snapshot)) {
            ++it->second.apply_reject_count;
            return false;
        }
        ++it->second.snapshot_count;
        break;
    }
    case internal::EventType::kDelta: {
        const auto* delta = std::get_if<internal::DeltaData>(&event.data);
        if (delta == nullptr || !apply_delta_data(it->second, *delta)) {
            ++it->second.apply_reject_count;
            return false;
        }
        ++it->second.delta_count;
        break;
    }
    case internal::EventType::kTrade: {
        const auto* trade = std::get_if<internal::TradeData>(&event.data);
        if (trade == nullptr) {
            ++it->second.apply_reject_count;
            return false;
        }
        it->second.last_trade = *trade;
        ++it->second.trade_count;
        break;
    }
    case internal::EventType::kHeartbeat:
    case internal::EventType::kStatus:
    case internal::EventType::kUnknown:
        ++it->second.apply_reject_count;
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
