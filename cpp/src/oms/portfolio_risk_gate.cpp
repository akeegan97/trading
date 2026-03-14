#include "trading/oms/portfolio_risk_gate.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace trading::oms {
namespace {

internal::QtyLots abs_qty(internal::QtyLots value) {
    return static_cast<internal::QtyLots>(std::llabs(value));
}

internal::QtyLots signed_intent_qty(const internal::OrderIntent& intent) {
    if (intent.qty_lots <= 0) {
        return 0;
    }
    switch (intent.side) {
    case internal::Side::kBuy:
    case internal::Side::kBid:
        return intent.qty_lots;
    case internal::Side::kSell:
    case internal::Side::kAsk:
        return -intent.qty_lots;
    case internal::Side::kUnknown:
        return 0;
    }
    return 0;
}

} // namespace

PortfolioRiskDecision PortfolioRiskDecision::allow_decision() { return PortfolioRiskDecision{}; }

PortfolioRiskDecision PortfolioRiskDecision::reject(PortfolioRiskRejectCode code,
                                                    std::string reason_message) {
    return PortfolioRiskDecision{
        .allow = false,
        .code = code,
        .reason_message = std::move(reason_message),
    };
}

PortfolioRiskGate::PortfolioRiskGate(PortfolioRiskConfig config) : config_(config) {}

const PortfolioRiskConfig& PortfolioRiskGate::config() const { return config_; }

PortfolioRiskDecision PortfolioRiskGate::evaluate(const internal::OrderIntent& intent,
                                                  const PortfolioRiskSnapshot& snapshot) const {
    // Portfolio risk applies only to order-creating flow.
    if (intent.action == internal::OmsAction::kCancel) {
        return PortfolioRiskDecision::allow_decision();
    }
    if (intent.action != internal::OmsAction::kPlace &&
        intent.action != internal::OmsAction::kReplace) {
        return PortfolioRiskDecision::allow_decision();
    }

    if (config_.enforce_realized_pnl_floor &&
        snapshot.realized_pnl_ticks_total < config_.min_realized_pnl_ticks) {
        return PortfolioRiskDecision::reject(
            PortfolioRiskRejectCode::kRealizedPnlFloor,
            "portfolio risk reject: realized_pnl_ticks_total breached floor");
    }

    const internal::QtyLots intent_delta = signed_intent_qty(intent);
    if (intent_delta == 0) {
        return PortfolioRiskDecision::reject(PortfolioRiskRejectCode::kInvalidIntent,
                                             "portfolio risk reject: side/qty is invalid");
    }

    const internal::QtyLots projected_market_position = snapshot.net_position_market + intent_delta;
    if (config_.max_abs_net_position_per_market > 0 &&
        abs_qty(projected_market_position) > config_.max_abs_net_position_per_market) {
        return PortfolioRiskDecision::reject(
            PortfolioRiskRejectCode::kAbsNetPositionMarketLimit,
            "portfolio risk reject: max_abs_net_position_per_market exceeded");
    }

    const internal::QtyLots projected_gross_global =
        snapshot.gross_position_global - abs_qty(snapshot.net_position_market) +
        abs_qty(projected_market_position);
    if (config_.max_abs_position_gross_global > 0 &&
        projected_gross_global > config_.max_abs_position_gross_global) {
        return PortfolioRiskDecision::reject(
            PortfolioRiskRejectCode::kAbsPositionGrossGlobalLimit,
            "portfolio risk reject: max_abs_position_gross_global exceeded");
    }

    return PortfolioRiskDecision::allow_decision();
}

std::string_view PortfolioRiskGate::reject_code_name(PortfolioRiskRejectCode code) {
    switch (code) {
    case PortfolioRiskRejectCode::kNone:
        return "none";
    case PortfolioRiskRejectCode::kInvalidIntent:
        return "invalid_intent";
    case PortfolioRiskRejectCode::kAbsNetPositionMarketLimit:
        return "abs_net_position_market_limit";
    case PortfolioRiskRejectCode::kAbsPositionGrossGlobalLimit:
        return "abs_position_gross_global_limit";
    case PortfolioRiskRejectCode::kRealizedPnlFloor:
        return "realized_pnl_floor";
    }
    return "unknown";
}

} // namespace trading::oms
