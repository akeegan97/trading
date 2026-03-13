#include "trading/oms/global_risk_gate.hpp"

#include <algorithm>
#include <utility>

namespace trading::oms {
namespace {

internal::QtyLots clamp_non_negative(internal::QtyLots value) {
    return std::max<internal::QtyLots>(value, 0);
}

} // namespace

GlobalRiskDecision GlobalRiskDecision::allow_decision() { return GlobalRiskDecision{}; }

GlobalRiskDecision GlobalRiskDecision::reject(GlobalRiskRejectCode code,
                                              std::string reason_message) {
    return GlobalRiskDecision{
        .allow = false,
        .code = code,
        .reason_message = std::move(reason_message),
    };
}

GlobalRiskGate::GlobalRiskGate(GlobalRiskConfig config) : config_(config) {}

const GlobalRiskConfig& GlobalRiskGate::config() const { return config_; }

GlobalRiskDecision GlobalRiskGate::evaluate(const internal::OrderIntent& intent,
                                            const GlobalRiskSnapshot& snapshot) const {
    // Global risk applies to order-creating flow; cancels are always allowed.
    if (intent.action == internal::OmsAction::kCancel) {
        return GlobalRiskDecision::allow_decision();
    }
    if (intent.action != internal::OmsAction::kPlace &&
        intent.action != internal::OmsAction::kReplace) {
        return GlobalRiskDecision::allow_decision();
    }

    if (config_.max_active_orders_global > 0 &&
        snapshot.active_orders_global >= config_.max_active_orders_global) {
        return GlobalRiskDecision::reject(GlobalRiskRejectCode::kActiveOrdersGlobalLimit,
                                          "global risk reject: max_active_orders_global exceeded");
    }
    if (config_.max_active_orders_per_market > 0 &&
        snapshot.active_orders_market >= config_.max_active_orders_per_market) {
        return GlobalRiskDecision::reject(
            GlobalRiskRejectCode::kActiveOrdersMarketLimit,
            "global risk reject: max_active_orders_per_market exceeded");
    }

    const internal::QtyLots intent_qty = clamp_non_negative(intent.qty_lots);
    if (config_.max_outstanding_qty_global > 0 &&
        snapshot.outstanding_qty_global + intent_qty > config_.max_outstanding_qty_global) {
        return GlobalRiskDecision::reject(
            GlobalRiskRejectCode::kOutstandingQtyGlobalLimit,
            "global risk reject: max_outstanding_qty_global exceeded");
    }
    if (config_.max_outstanding_qty_per_market > 0 &&
        snapshot.outstanding_qty_market + intent_qty > config_.max_outstanding_qty_per_market) {
        return GlobalRiskDecision::reject(
            GlobalRiskRejectCode::kOutstandingQtyMarketLimit,
            "global risk reject: max_outstanding_qty_per_market exceeded");
    }

    return GlobalRiskDecision::allow_decision();
}

std::string_view GlobalRiskGate::reject_code_name(GlobalRiskRejectCode code) {
    switch (code) {
    case GlobalRiskRejectCode::kNone:
        return "none";
    case GlobalRiskRejectCode::kActiveOrdersGlobalLimit:
        return "active_orders_global_limit";
    case GlobalRiskRejectCode::kActiveOrdersMarketLimit:
        return "active_orders_market_limit";
    case GlobalRiskRejectCode::kOutstandingQtyGlobalLimit:
        return "outstanding_qty_global_limit";
    case GlobalRiskRejectCode::kOutstandingQtyMarketLimit:
        return "outstanding_qty_market_limit";
    }
    return "unknown";
}

} // namespace trading::oms
