#include "trading/strategy/shard_risk_gate.hpp"

#include <cstdlib>
#include <utility>

namespace trading::strategy {
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

ShardRiskDecision ShardRiskDecision::allow_decision() { return ShardRiskDecision{}; }

ShardRiskDecision ShardRiskDecision::reject(ShardRiskRejectCode code, std::string reason_message) {
    return ShardRiskDecision{
        .allow = false,
        .code = code,
        .reason_message = std::move(reason_message),
    };
}

ShardRiskGate::ShardRiskGate(ShardRiskConfig config) : config_(config) {}

const ShardRiskConfig& ShardRiskGate::config() const { return config_; }

ShardRiskDecision ShardRiskGate::evaluate(const internal::OrderIntent& intent,
                                          const ShardRiskSnapshot& snapshot) const {
    if (intent.action == internal::OmsAction::kCancel) {
        return ShardRiskDecision::allow_decision();
    }
    if (intent.action != internal::OmsAction::kPlace && intent.action != internal::OmsAction::kReplace) {
        return ShardRiskDecision::reject(ShardRiskRejectCode::kInvalidIntent,
                                         "shard risk reject: action is invalid");
    }

    const internal::QtyLots intent_signed_qty = signed_intent_qty(intent);
    if (intent_signed_qty == 0) {
        return ShardRiskDecision::reject(ShardRiskRejectCode::kInvalidIntent,
                                         "shard risk reject: side/qty is invalid");
    }

    if (config_.max_open_orders_per_market > 0 &&
        snapshot.open_orders_market + 1 > config_.max_open_orders_per_market) {
        return ShardRiskDecision::reject(
            ShardRiskRejectCode::kOpenOrdersPerMarketLimit,
            "shard risk reject: max_open_orders_per_market exceeded");
    }

    if (config_.max_working_qty_per_market > 0 &&
        snapshot.working_qty_market + intent.qty_lots > config_.max_working_qty_per_market) {
        return ShardRiskDecision::reject(
            ShardRiskRejectCode::kWorkingQtyPerMarketLimit,
            "shard risk reject: max_working_qty_per_market exceeded");
    }

    const internal::QtyLots projected_net_position = snapshot.net_position_market + intent_signed_qty;
    if (config_.max_abs_net_position_per_market > 0 &&
        abs_qty(projected_net_position) > config_.max_abs_net_position_per_market) {
        return ShardRiskDecision::reject(
            ShardRiskRejectCode::kAbsNetPositionPerMarketLimit,
            "shard risk reject: max_abs_net_position_per_market exceeded");
    }

    return ShardRiskDecision::allow_decision();
}

std::string_view ShardRiskGate::reject_code_name(ShardRiskRejectCode code) {
    switch (code) {
    case ShardRiskRejectCode::kNone:
        return "none";
    case ShardRiskRejectCode::kInvalidIntent:
        return "invalid_intent";
    case ShardRiskRejectCode::kOpenOrdersPerMarketLimit:
        return "open_orders_per_market_limit";
    case ShardRiskRejectCode::kWorkingQtyPerMarketLimit:
        return "working_qty_per_market_limit";
    case ShardRiskRejectCode::kAbsNetPositionPerMarketLimit:
        return "abs_net_position_per_market_limit";
    }
    return "unknown";
}

} // namespace trading::strategy
