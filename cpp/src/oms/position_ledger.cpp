#include "trading/oms/position_ledger.hpp"

#include <utility>

namespace trading::oms {

bool PositionLedger::on_order_update(const internal::OrderStateUpdate& update) {
    std::scoped_lock lock{mutex_};
    std::string error_message;
    const bool result_ok = core_.on_order_update(update, error_message);
    if (!result_ok) {
        last_error_ = std::move(error_message);
    }
    return result_ok;
}

PortfolioRiskSnapshot PositionLedger::snapshot_for(internal::ExchangeId exchange,
                                                   std::string_view market_ticker) const {
    std::scoped_lock lock{mutex_};
    return core_.portfolio_risk_snapshot(exchange, market_ticker);
}

std::optional<PositionSnapshot> PositionLedger::market_position(internal::ExchangeId exchange,
                                                                std::string_view market_ticker) const {
    std::scoped_lock lock{mutex_};
    return core_.market_position(exchange, market_ticker);
}

PositionLedgerStats PositionLedger::stats() const {
    std::scoped_lock lock{mutex_};
    return core_.stats();
}

std::string PositionLedger::last_error() const {
    std::scoped_lock lock{mutex_};
    return last_error_;
}

} // namespace trading::oms
