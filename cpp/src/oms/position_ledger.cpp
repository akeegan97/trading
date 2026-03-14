#include "trading/oms/position_ledger.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace trading::oms {

bool PositionLedger::on_order_update(const internal::OrderStateUpdate& update) {
    if (update.status != internal::OmsOrderStatus::kFilled &&
        update.status != internal::OmsOrderStatus::kPartiallyFilled) {
        std::scoped_lock lock{mutex_};
        ++ignored_update_count_;
        return true;
    }

    const auto* fill = std::get_if<internal::OrderFill>(&update.data);
    if (fill == nullptr) {
        std::scoped_lock lock{mutex_};
        ++rejected_fill_count_;
        set_error("Fill status update is missing OrderFill payload");
        return false;
    }

    std::scoped_lock lock{mutex_};
    return apply_fill(update, *fill);
}

std::optional<PositionSnapshot> PositionLedger::market_position(internal::ExchangeId exchange,
                                                                std::string_view market_ticker) const {
    std::scoped_lock lock{mutex_};
    const auto position_it = positions_.find(MarketKey{
        .exchange = exchange,
        .market_ticker = std::string{market_ticker},
    });
    if (position_it == positions_.end()) {
        return std::nullopt;
    }

    const MarketPositionState& position = position_it->second;
    return PositionSnapshot{
        .exchange = position.exchange,
        .market_ticker = position.market_ticker,
        .net_qty_lots = position.net_qty_lots,
        .avg_open_price_ticks =
            position.has_open_price ? std::optional<internal::PriceTicks>{position.avg_open_price_ticks}
                                    : std::nullopt,
        .bought_qty_lots = position.bought_qty_lots,
        .sold_qty_lots = position.sold_qty_lots,
        .closed_qty_lots = position.closed_qty_lots,
        .realized_pnl_ticks = position.realized_pnl_ticks,
        .fill_count = position.fill_count,
        .last_fill_ts_ns = position.last_fill_ts_ns,
    };
}

PositionLedgerStats PositionLedger::stats() const {
    std::scoped_lock lock{mutex_};
    std::size_t open_position_count = 0;
    for (const auto& [key, position] : positions_) {
        static_cast<void>(key);
        if (position.net_qty_lots != 0) {
            ++open_position_count;
        }
    }

    return PositionLedgerStats{
        .processed_fill_count = processed_fill_count_,
        .ignored_update_count = ignored_update_count_,
        .rejected_fill_count = rejected_fill_count_,
        .tracked_market_count = positions_.size(),
        .open_position_count = open_position_count,
        .closed_position_count = closed_position_count_,
        .realized_pnl_ticks_total = realized_pnl_ticks_total_,
    };
}

std::string PositionLedger::last_error() const {
    std::scoped_lock lock{mutex_};
    return last_error_;
}

bool PositionLedger::apply_fill(const internal::OrderStateUpdate& update,
                                const internal::OrderFill& fill) {
    const std::string& market_ticker = fill.market_ticker.empty() ? update.market_ticker : fill.market_ticker;
    if (market_ticker.empty()) {
        ++rejected_fill_count_;
        set_error("Fill update is missing market_ticker");
        return false;
    }
    if (fill.fill_qty_lots <= 0) {
        ++rejected_fill_count_;
        set_error("Fill update has non-positive fill_qty_lots");
        return false;
    }

    const internal::QtyLots signed_fill_qty = side_to_signed_qty(fill.side, fill.fill_qty_lots);
    if (signed_fill_qty == 0) {
        ++rejected_fill_count_;
        set_error("Fill update has unknown side");
        return false;
    }

    MarketPositionState& position = positions_[MarketKey{
        .exchange = update.exchange,
        .market_ticker = market_ticker,
    }];
    position.exchange = update.exchange;
    position.market_ticker = market_ticker;

    position.fill_count += 1;
    position.last_fill_ts_ns = update.recv_ts_ns;
    if (signed_fill_qty > 0) {
        position.bought_qty_lots += fill.fill_qty_lots;
    } else {
        position.sold_qty_lots += fill.fill_qty_lots;
    }

    const internal::QtyLots old_net_qty = position.net_qty_lots;
    if (old_net_qty == 0) {
        position.net_qty_lots = signed_fill_qty;
        position.avg_open_price_ticks = fill.fill_price_ticks;
        position.has_open_price = true;
        ++processed_fill_count_;
        return true;
    }

    const bool same_direction = (old_net_qty > 0 && signed_fill_qty > 0) ||
                                (old_net_qty < 0 && signed_fill_qty < 0);
    if (same_direction) {
        const internal::QtyLots old_abs = abs_qty(old_net_qty);
        const internal::QtyLots fill_abs = abs_qty(signed_fill_qty);
        const internal::QtyLots new_abs = old_abs + fill_abs;
        const std::int64_t weighted_notional =
            static_cast<std::int64_t>(position.avg_open_price_ticks) * old_abs +
            static_cast<std::int64_t>(fill.fill_price_ticks) * fill_abs;
        position.avg_open_price_ticks = static_cast<internal::PriceTicks>(weighted_notional / new_abs);
        position.net_qty_lots = old_net_qty + signed_fill_qty;
        position.has_open_price = true;
        ++processed_fill_count_;
        return true;
    }

    const internal::QtyLots old_abs = abs_qty(old_net_qty);
    const internal::QtyLots fill_abs = abs_qty(signed_fill_qty);
    const internal::QtyLots closed_qty = std::min(old_abs, fill_abs);
    if (old_net_qty > 0) {
        const std::int64_t pnl_ticks =
            static_cast<std::int64_t>(fill.fill_price_ticks - position.avg_open_price_ticks) * closed_qty;
        position.realized_pnl_ticks += pnl_ticks;
        realized_pnl_ticks_total_ += pnl_ticks;
    } else {
        const std::int64_t pnl_ticks =
            static_cast<std::int64_t>(position.avg_open_price_ticks - fill.fill_price_ticks) * closed_qty;
        position.realized_pnl_ticks += pnl_ticks;
        realized_pnl_ticks_total_ += pnl_ticks;
    }
    position.closed_qty_lots += closed_qty;
    position.net_qty_lots = old_net_qty + signed_fill_qty;

    if (position.net_qty_lots == 0) {
        position.has_open_price = false;
        ++closed_position_count_;
    } else {
        const bool flipped_direction = (old_net_qty > 0 && position.net_qty_lots < 0) ||
                                       (old_net_qty < 0 && position.net_qty_lots > 0);
        if (flipped_direction) {
            position.avg_open_price_ticks = fill.fill_price_ticks;
            position.has_open_price = true;
        }
    }

    ++processed_fill_count_;
    return true;
}

internal::QtyLots PositionLedger::abs_qty(internal::QtyLots value) {
    return value >= 0 ? value : -value;
}

internal::QtyLots PositionLedger::side_to_signed_qty(internal::Side side, internal::QtyLots qty_lots) {
    switch (side) {
    case internal::Side::kBuy:
    case internal::Side::kBid:
        return qty_lots;
    case internal::Side::kSell:
    case internal::Side::kAsk:
        return -qty_lots;
    case internal::Side::kUnknown:
        return 0;
    }
    return 0;
}

void PositionLedger::set_error(std::string_view error_message) { last_error_.assign(error_message); }

} // namespace trading::oms
