#include "trading/oms/order_manager_core.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace trading::oms {

namespace {
internal::TimestampNs to_timestamp_ns(std::chrono::steady_clock::time_point time_point) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(time_point.time_since_epoch());
    return elapsed.count() > 0 ? static_cast<internal::TimestampNs>(elapsed.count()) : 0U;
}
} // namespace

OrderManagerCore::OrderManagerCore(GlobalRiskConfig global_risk_config,
                                   PortfolioRiskConfig portfolio_risk_config)
    : global_risk_gate_(global_risk_config), portfolio_risk_gate_(portfolio_risk_config) {}

void OrderManagerCore::reset() {
    in_flight_orders_.clear();
    exchange_to_client_.clear();
    risk_reject_count_ = 0;
    portfolio_risk_reject_count_ = 0;
    transition_applied_count_ = 0;
    transition_reject_count_ = 0;
    unknown_order_update_count_ = 0;
    policy_reject_count_ = 0;
}

OrderManagerCoreStats OrderManagerCore::stats() const {
    std::size_t active_count = 0;
    for (const auto& [client_order_id, order] : in_flight_orders_) {
        static_cast<void>(client_order_id);
        if (!is_terminal_state(order.status)) {
            ++active_count;
        }
    }

    return OrderManagerCoreStats{
        .risk_reject_count = risk_reject_count_,
        .portfolio_risk_reject_count = portfolio_risk_reject_count_,
        .transition_applied_count = transition_applied_count_,
        .transition_reject_count = transition_reject_count_,
        .unknown_order_update_count = unknown_order_update_count_,
        .policy_reject_count = policy_reject_count_,
        .tracked_order_count = in_flight_orders_.size(),
        .active_order_count = active_count,
    };
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
bool OrderManagerCore::validate_and_track_submission(internal::OrderRequestId request_id,
                                                     const internal::OrderIntent& intent,
                                                     std::string& error_message) {
    if (intent.client_order_id.empty()) {
        ++policy_reject_count_;
        error_message = "Order intent must include client_order_id";
        return false;
    }

    if (intent.action == internal::OmsAction::kPlace) {
        if (in_flight_orders_.contains(intent.client_order_id)) {
            ++policy_reject_count_;
            error_message = "Order intent reuses an existing client_order_id";
            return false;
        }

        InFlightOrder order{
            .request_id = request_id,
            .last_action = intent.action,
            .status = InFlightStatus::kPending,
            .client_order_id = intent.client_order_id,
            .market_ticker = intent.market_ticker,
            .side = intent.side,
            .requested_qty_lots = intent.qty_lots,
            .created_ts_ns = intent.intent_ts_ns,
        };
        in_flight_orders_[order.client_order_id] = std::move(order);
        return true;
    }

    if (intent.action != internal::OmsAction::kCancel &&
        intent.action != internal::OmsAction::kReplace) {
        ++policy_reject_count_;
        error_message = "Unsupported OMS action for policy validation";
        return false;
    }

    std::optional<internal::ClientOrderId> target_by_client_id;
    if (intent.target_client_order_id.has_value()) {
        target_by_client_id = *intent.target_client_order_id;
    } else if (intent.action == internal::OmsAction::kCancel) {
        target_by_client_id = intent.client_order_id;
    }

    std::optional<internal::ClientOrderId> target_by_exchange_id;
    if (intent.target_exchange_order_id.has_value()) {
        const auto exchange_it = exchange_to_client_.find(*intent.target_exchange_order_id);
        if (exchange_it == exchange_to_client_.end()) {
            ++policy_reject_count_;
            error_message = "OMS target_exchange_order_id is unknown";
            return false;
        }
        target_by_exchange_id = exchange_it->second;
    }

    if (target_by_client_id.has_value() && target_by_exchange_id.has_value() &&
        *target_by_client_id != *target_by_exchange_id) {
        ++policy_reject_count_;
        error_message = "OMS target ids resolve to different orders";
        return false;
    }

    internal::ClientOrderId resolved_target_id;
    if (target_by_client_id.has_value()) {
        resolved_target_id = *target_by_client_id;
    } else if (target_by_exchange_id.has_value()) {
        resolved_target_id = *target_by_exchange_id;
    } else {
        ++policy_reject_count_;
        error_message = "OMS intent is missing a resolvable target order";
        return false;
    }

    const auto target_it = in_flight_orders_.find(resolved_target_id);
    if (target_it == in_flight_orders_.end()) {
        ++policy_reject_count_;
        error_message = "OMS target order is not tracked";
        return false;
    }
    if (is_terminal_state(target_it->second.status)) {
        ++policy_reject_count_;
        error_message = "OMS target order is terminal";
        return false;
    }

    if (intent.action == internal::OmsAction::kReplace) {
        if (resolved_target_id == intent.client_order_id) {
            ++policy_reject_count_;
            error_message = "OMS replace requires a new client_order_id";
            return false;
        }
        if (in_flight_orders_.contains(intent.client_order_id)) {
            ++policy_reject_count_;
            error_message = "OMS replace client_order_id already exists";
            return false;
        }
        if (!intent.market_ticker.empty() && !target_it->second.market_ticker.empty() &&
            intent.market_ticker != target_it->second.market_ticker) {
            ++policy_reject_count_;
            error_message = "OMS replace target market mismatch";
            return false;
        }

        InFlightOrder order{
            .request_id = request_id,
            .last_action = intent.action,
            .status = InFlightStatus::kPending,
            .client_order_id = intent.client_order_id,
            .replace_target_client_order_id = resolved_target_id,
            .market_ticker = !intent.market_ticker.empty() ? intent.market_ticker
                                                           : target_it->second.market_ticker,
            .side = intent.side != internal::Side::kUnknown ? intent.side : target_it->second.side,
            .requested_qty_lots =
                intent.qty_lots > 0 ? intent.qty_lots : target_it->second.requested_qty_lots,
            .created_ts_ns = intent.intent_ts_ns,
        };
        in_flight_orders_[order.client_order_id] = std::move(order);
    }

    return true;
}
// NOLINTEND(readability-function-cognitive-complexity)

std::optional<InFlightOrderSnapshot>
OrderManagerCore::in_flight_order(std::string_view client_order_id) const {
    const auto order_it = in_flight_orders_.find(std::string{client_order_id});
    if (order_it == in_flight_orders_.end()) {
        return std::nullopt;
    }

    const InFlightOrder& order = order_it->second;
    return InFlightOrderSnapshot{
        .request_id = order.request_id,
        .last_action = order.last_action,
        .status = order.status,
        .client_order_id = order.client_order_id,
        .exchange_order_id = order.exchange_order_id,
        .replace_target_client_order_id = order.replace_target_client_order_id,
        .market_ticker = order.market_ticker,
        .side = order.side,
        .requested_qty_lots = order.requested_qty_lots,
        .filled_qty_lots = order.filled_qty_lots,
        .created_ts_ns = order.created_ts_ns,
        .last_update_ts_ns = order.last_update_ts_ns,
    };
}

std::optional<GlobalRiskDecision>
OrderManagerCore::evaluate_global_risk(const internal::OrderIntent& intent, std::string& error_message) {
    const auto snapshot = build_risk_snapshot(intent.market_ticker, intent.client_order_id);
    auto decision = global_risk_gate_.evaluate(intent, snapshot);
    if (decision.allow) {
        return std::nullopt;
    }

    ++risk_reject_count_;
    error_message = decision.reason_message;
    return decision;
}

std::optional<PortfolioRiskDecision>
OrderManagerCore::evaluate_portfolio_risk(const internal::OrderIntent& intent,
                                          const PortfolioRiskSnapshot& snapshot,
                                          std::string& error_message) {
    auto decision = portfolio_risk_gate_.evaluate(intent, snapshot);
    if (decision.allow) {
        return std::nullopt;
    }

    ++portfolio_risk_reject_count_;
    error_message = decision.reason_message;
    return decision;
}

internal::OrderStateUpdate
OrderManagerCore::make_global_risk_reject_update(const internal::OrderIntent& intent,
                                                 const GlobalRiskDecision& decision) {
    return internal::OrderStateUpdate{
        .exchange = intent.exchange,
        .status = internal::OmsOrderStatus::kRejected,
        .client_order_id = intent.client_order_id,
        .exchange_order_id = std::nullopt,
        .market_ticker = intent.market_ticker,
        .recv_ts_ns = monotonic_now_ns(),
        .data =
            internal::OrderReject{
                .client_order_id = intent.client_order_id,
                .exchange_order_id = std::nullopt,
                .reason_code = std::string{GlobalRiskGate::reject_code_name(decision.code)},
                .reason_message = decision.reason_message,
            },
        .raw_payload = {},
    };
}

internal::OrderStateUpdate
OrderManagerCore::make_portfolio_risk_reject_update(const internal::OrderIntent& intent,
                                                    const PortfolioRiskDecision& decision) {
    return internal::OrderStateUpdate{
        .exchange = intent.exchange,
        .status = internal::OmsOrderStatus::kRejected,
        .client_order_id = intent.client_order_id,
        .exchange_order_id = std::nullopt,
        .market_ticker = intent.market_ticker,
        .recv_ts_ns = monotonic_now_ns(),
        .data =
            internal::OrderReject{
                .client_order_id = intent.client_order_id,
                .exchange_order_id = std::nullopt,
                .reason_code = std::string{PortfolioRiskGate::reject_code_name(decision.code)},
                .reason_message = decision.reason_message,
            },
        .raw_payload = {},
    };
}

bool OrderManagerCore::apply_update_transition(internal::OrderStateUpdate& update,
                                               std::string& error_message) {
    const auto next_status = to_in_flight_status(update.status);
    if (!next_status.has_value()) {
        ++transition_reject_count_;
        error_message = "Unsupported OMS transition status";
        return false;
    }

    auto order_it = in_flight_orders_.find(update.client_order_id);
    if (order_it == in_flight_orders_.end() && update.exchange_order_id.has_value()) {
        const auto exchange_it = exchange_to_client_.find(*update.exchange_order_id);
        if (exchange_it != exchange_to_client_.end()) {
            order_it = in_flight_orders_.find(exchange_it->second);
        }
    }

    if (order_it == in_flight_orders_.end()) {
        ++unknown_order_update_count_;
        error_message = "Order update does not match an in-flight order";
        return false;
    }

    InFlightOrder& order = order_it->second;
    if (!can_transition(order.status, *next_status)) {
        ++transition_reject_count_;
        error_message = "Rejected OMS transition for in-flight order";
        return false;
    }

    order.status = *next_status;
    if (update.recv_ts_ns != 0) {
        order.last_update_ts_ns = update.recv_ts_ns;
    } else {
        order.last_update_ts_ns = monotonic_now_ns();
    }
    if (!update.market_ticker.empty()) {
        order.market_ticker = update.market_ticker;
    }
    if (update.exchange_order_id.has_value() && !update.exchange_order_id->empty()) {
        order.exchange_order_id = *update.exchange_order_id;
        exchange_to_client_[*update.exchange_order_id] = order.client_order_id;
    }

    if (auto* fill = std::get_if<internal::OrderFill>(&update.data); fill != nullptr) {
        if (fill->side == internal::Side::kUnknown && order.side != internal::Side::kUnknown) {
            fill->side = order.side;
        }
        if (order.side == internal::Side::kUnknown && fill->side != internal::Side::kUnknown) {
            order.side = fill->side;
        }
        if (*next_status == InFlightStatus::kFilled && order.requested_qty_lots > 0) {
            order.filled_qty_lots = order.requested_qty_lots;
        } else if (fill->fill_qty_lots > order.filled_qty_lots) {
            order.filled_qty_lots = fill->fill_qty_lots;
        }
    }

    ++transition_applied_count_;
    return true;
}

bool OrderManagerCore::is_terminal_state(InFlightStatus status) {
    switch (status) {
    case InFlightStatus::kFilled:
    case InFlightStatus::kCanceled:
    case InFlightStatus::kRejected:
        return true;
    case InFlightStatus::kPending:
    case InFlightStatus::kAccepted:
    case InFlightStatus::kPartiallyFilled:
    case InFlightStatus::kReplaced:
        return false;
    }
    return false;
}

std::optional<InFlightStatus> OrderManagerCore::to_in_flight_status(internal::OmsOrderStatus status) {
    switch (status) {
    case internal::OmsOrderStatus::kAccepted:
        return InFlightStatus::kAccepted;
    case internal::OmsOrderStatus::kRejected:
        return InFlightStatus::kRejected;
    case internal::OmsOrderStatus::kCanceled:
        return InFlightStatus::kCanceled;
    case internal::OmsOrderStatus::kPartiallyFilled:
        return InFlightStatus::kPartiallyFilled;
    case internal::OmsOrderStatus::kFilled:
        return InFlightStatus::kFilled;
    case internal::OmsOrderStatus::kReplaced:
        return InFlightStatus::kReplaced;
    case internal::OmsOrderStatus::kUnknown:
        return std::nullopt;
    }
    return std::nullopt;
}

bool OrderManagerCore::can_transition(InFlightStatus current, InFlightStatus next) {
    if (current == next) {
        return true;
    }

    switch (current) {
    case InFlightStatus::kPending:
        return next == InFlightStatus::kAccepted || next == InFlightStatus::kPartiallyFilled ||
               next == InFlightStatus::kFilled || next == InFlightStatus::kCanceled ||
               next == InFlightStatus::kRejected;
    case InFlightStatus::kAccepted:
        return next == InFlightStatus::kPartiallyFilled || next == InFlightStatus::kFilled ||
               next == InFlightStatus::kCanceled || next == InFlightStatus::kRejected ||
               next == InFlightStatus::kReplaced;
    case InFlightStatus::kPartiallyFilled:
        return next == InFlightStatus::kFilled || next == InFlightStatus::kCanceled ||
               next == InFlightStatus::kRejected || next == InFlightStatus::kReplaced;
    case InFlightStatus::kReplaced:
        return next == InFlightStatus::kAccepted || next == InFlightStatus::kPartiallyFilled ||
               next == InFlightStatus::kFilled || next == InFlightStatus::kCanceled ||
               next == InFlightStatus::kRejected;
    case InFlightStatus::kFilled:
    case InFlightStatus::kCanceled:
    case InFlightStatus::kRejected:
        return false;
    }
    return false;
}

internal::QtyLots OrderManagerCore::outstanding_qty(const InFlightOrder& order) {
    if (order.requested_qty_lots <= 0) {
        return 0;
    }
    if (order.filled_qty_lots <= 0) {
        return order.requested_qty_lots;
    }
    if (order.filled_qty_lots >= order.requested_qty_lots) {
        return 0;
    }
    return order.requested_qty_lots - order.filled_qty_lots;
}

internal::TimestampNs OrderManagerCore::monotonic_now_ns() {
    return to_timestamp_ns(std::chrono::steady_clock::now());
}

GlobalRiskSnapshot OrderManagerCore::build_risk_snapshot(std::string_view market_ticker,
                                                         std::string_view skip_client_order_id) const {
    GlobalRiskSnapshot snapshot{};
    for (const auto& [client_order_id, order] : in_flight_orders_) {
        if (!skip_client_order_id.empty() && client_order_id == skip_client_order_id) {
            continue;
        }
        if (is_terminal_state(order.status)) {
            continue;
        }

        ++snapshot.active_orders_global;
        snapshot.outstanding_qty_global += outstanding_qty(order);
        if (!market_ticker.empty() && order.market_ticker == market_ticker) {
            ++snapshot.active_orders_market;
            snapshot.outstanding_qty_market += outstanding_qty(order);
        }
    }
    return snapshot;
}

} // namespace trading::oms
