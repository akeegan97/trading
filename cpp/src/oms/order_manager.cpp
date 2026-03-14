#include "trading/oms/order_manager.hpp"

#include <chrono>
#include <exception>
#include <string_view>
#include <thread>
#include <utility>

namespace trading::oms {

namespace {
internal::TimestampNs to_timestamp_ns(std::chrono::steady_clock::time_point time_point) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(time_point.time_since_epoch());
    return elapsed.count() > 0 ? static_cast<internal::TimestampNs>(elapsed.count()) : 0U;
}
} // namespace

OrderManager::OrderManager(const IExchangeOmsAdapter& adapter, IOrderTransport& transport,
                           IOrderEventSink& event_sink, OrderManagerConfig config)
    : adapter_(adapter), transport_(transport), event_sink_(event_sink), config_(std::move(config)),
      global_risk_gate_(config_.global_risk) {}

OrderManager::~OrderManager() { stop(); }

bool OrderManager::start() {
    if (worker_.joinable()) {
        return false;
    }

    set_error("");
    submitted_count_.store(0, std::memory_order_relaxed);
    sent_count_.store(0, std::memory_order_relaxed);
    send_failed_count_.store(0, std::memory_order_relaxed);
    risk_reject_count_.store(0, std::memory_order_relaxed);
    receive_count_.store(0, std::memory_order_relaxed);
    parse_failed_count_.store(0, std::memory_order_relaxed);
    update_drop_count_.store(0, std::memory_order_relaxed);
    transition_applied_count_.store(0, std::memory_order_relaxed);
    transition_reject_count_.store(0, std::memory_order_relaxed);
    unknown_order_update_count_.store(0, std::memory_order_relaxed);
    policy_reject_count_.store(0, std::memory_order_relaxed);
    unsupported_intent_count_.store(0, std::memory_order_relaxed);
    {
        std::scoped_lock in_flight_lock{in_flight_mutex_};
        in_flight_orders_.clear();
        exchange_to_client_.clear();
    }

    if (!transport_.connect(config_.transport)) {
        set_error(transport_.last_error());
        return false;
    }

    // Publish started state before worker loop.
    running_.store(true, std::memory_order_release);
    worker_ = std::jthread([this](const std::stop_token& stop_token) { run(stop_token); });
    return true;
}

void OrderManager::stop() {
    if (!worker_.joinable()) {
        running_.store(false, std::memory_order_release);
        return;
    }

    worker_.request_stop();
    transport_.close();
    worker_.join();
    running_.store(false, std::memory_order_release);
}

// Acquire pairs with start/stop release stores.
bool OrderManager::running() const { return running_.load(std::memory_order_acquire); }

std::optional<internal::OrderRequestId> OrderManager::submit(internal::OrderIntent intent) {
    if (!running()) {
        set_error("OrderManager is not running");
        return std::nullopt;
    }
    if (!adapter_.supports(intent)) {
        unsupported_intent_count_.fetch_add(1, std::memory_order_relaxed);
        set_error("Order intent is not supported by adapter");
        return std::nullopt;
    }

    if (intent.intent_ts_ns == 0) {
        intent.intent_ts_ns = monotonic_now_ns();
    }
    const internal::OrderRequestId request_id =
        next_request_id_.fetch_add(1, std::memory_order_relaxed);

    std::string policy_error;
    if (!validate_and_track_submission(request_id, intent, policy_error)) {
        policy_reject_count_.fetch_add(1, std::memory_order_relaxed);
        set_error(policy_error);
        return std::nullopt;
    }
    {
        std::scoped_lock queue_lock{queue_mutex_};
        outbound_queue_.push_back(PendingIntent{
            .request_id = request_id,
            .intent = std::move(intent),
        });
    }
    submitted_count_.fetch_add(1, std::memory_order_relaxed);
    return request_id;
}

OrderManagerStats OrderManager::stats() const {
    std::size_t pending_intent_count = 0;
    {
        std::scoped_lock queue_lock{queue_mutex_};
        pending_intent_count = outbound_queue_.size();
    }

    std::size_t tracked_order_count = 0;
    std::size_t active_count = 0;
    {
        std::scoped_lock in_flight_lock{in_flight_mutex_};
        tracked_order_count = in_flight_orders_.size();
        for (const auto& [client_order_id, order] : in_flight_orders_) {
            static_cast<void>(client_order_id);
            if (!is_terminal_state(order.status)) {
                ++active_count;
            }
        }
    }

    return OrderManagerStats{
        .submitted_count = submitted_count_.load(std::memory_order_relaxed),
        .sent_count = sent_count_.load(std::memory_order_relaxed),
        .send_failed_count = send_failed_count_.load(std::memory_order_relaxed),
        .risk_reject_count = risk_reject_count_.load(std::memory_order_relaxed),
        .receive_count = receive_count_.load(std::memory_order_relaxed),
        .parse_failed_count = parse_failed_count_.load(std::memory_order_relaxed),
        .update_drop_count = update_drop_count_.load(std::memory_order_relaxed),
        .transition_applied_count = transition_applied_count_.load(std::memory_order_relaxed),
        .transition_reject_count = transition_reject_count_.load(std::memory_order_relaxed),
        .unknown_order_update_count = unknown_order_update_count_.load(std::memory_order_relaxed),
        .policy_reject_count = policy_reject_count_.load(std::memory_order_relaxed),
        .unsupported_intent_count = unsupported_intent_count_.load(std::memory_order_relaxed),
        .pending_intent_count = pending_intent_count,
        .tracked_order_count = tracked_order_count,
        .active_order_count = active_count,
    };
}

std::string OrderManager::last_error() const {
    std::scoped_lock lock{error_mutex_};
    return last_error_;
}

std::optional<InFlightOrderSnapshot>
OrderManager::in_flight_order(std::string_view client_order_id) const {
    std::scoped_lock lock{in_flight_mutex_};
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

internal::TimestampNs OrderManager::monotonic_now_ns() {
    return to_timestamp_ns(std::chrono::steady_clock::now());
}

bool OrderManager::is_terminal_state(InFlightStatus status) {
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

std::optional<InFlightStatus> OrderManager::to_in_flight_status(internal::OmsOrderStatus status) {
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

bool OrderManager::can_transition(InFlightStatus current, InFlightStatus next) {
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

void OrderManager::run(const std::stop_token& stop_token) {
    while (!stop_token.stop_requested()) {
        const std::size_t drained_intents = drain_outbound_intents();
        const bool processed_update = pump_incoming_update();
        if (drained_intents == 0 && !processed_update) {
            std::this_thread::sleep_for(config_.loop_idle_sleep);
        }
    }

    transport_.close();
    running_.store(false, std::memory_order_release);
}

std::size_t OrderManager::drain_outbound_intents() {
    std::deque<PendingIntent> local_intents;
    {
        std::scoped_lock queue_lock{queue_mutex_};
        if (outbound_queue_.empty()) {
            return 0;
        }
        local_intents.swap(outbound_queue_);
    }

    std::size_t processed_count = 0;
    for (auto& pending_intent : local_intents) {
        ++processed_count;
        const auto risk_snapshot = build_risk_snapshot(pending_intent.intent.market_ticker,
                                                       pending_intent.intent.client_order_id);
        const auto risk_decision = global_risk_gate_.evaluate(pending_intent.intent, risk_snapshot);
        if (!risk_decision.allow) {
            risk_reject_count_.fetch_add(1, std::memory_order_relaxed);
            set_error(risk_decision.reason_message);
            emit_global_risk_reject(pending_intent, risk_decision);
            continue;
        }

        try {
            const std::string payload = adapter_.build_request(pending_intent.intent);
            if (!transport_.send_text(payload)) {
                send_failed_count_.fetch_add(1, std::memory_order_relaxed);
                set_error(transport_.last_error());
                continue;
            }
            sent_count_.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception& exception) {
            send_failed_count_.fetch_add(1, std::memory_order_relaxed);
            set_error(exception.what());
        } catch (...) {
            send_failed_count_.fetch_add(1, std::memory_order_relaxed);
            set_error("Unknown error building OMS request");
        }
    }

    return processed_count;
}

bool OrderManager::pump_incoming_update() {
    auto payload = transport_.recv_text();
    if (!payload.has_value()) {
        return false;
    }

    receive_count_.fetch_add(1, std::memory_order_relaxed);
    const auto parse_result = adapter_.parse_update(*payload);
    const auto& parsed_update = parse_result.value();
    if (!parse_result.ok() || !parsed_update.has_value()) {
        parse_failed_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    internal::OrderStateUpdate update = *parsed_update;
    if (!apply_update_transition(update)) {
        update_drop_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    if (!event_sink_.on_order_update(update)) {
        update_drop_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
bool OrderManager::validate_and_track_submission(internal::OrderRequestId request_id,
                                                 const internal::OrderIntent& intent,
                                                 std::string& error_message) {
    if (intent.client_order_id.empty()) {
        error_message = "Order intent must include client_order_id";
        return false;
    }

    std::scoped_lock lock{in_flight_mutex_};
    if (intent.action == internal::OmsAction::kPlace) {
        if (in_flight_orders_.contains(intent.client_order_id)) {
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
            error_message = "OMS target_exchange_order_id is unknown";
            return false;
        }
        target_by_exchange_id = exchange_it->second;
    }

    if (target_by_client_id.has_value() && target_by_exchange_id.has_value() &&
        *target_by_client_id != *target_by_exchange_id) {
        error_message = "OMS target ids resolve to different orders";
        return false;
    }

    internal::ClientOrderId resolved_target_id;
    if (target_by_client_id.has_value()) {
        resolved_target_id = *target_by_client_id;
    } else if (target_by_exchange_id.has_value()) {
        resolved_target_id = *target_by_exchange_id;
    } else {
        error_message = "OMS intent is missing a resolvable target order";
        return false;
    }

    const auto target_it = in_flight_orders_.find(resolved_target_id);
    if (target_it == in_flight_orders_.end()) {
        error_message = "OMS target order is not tracked";
        return false;
    }
    if (is_terminal_state(target_it->second.status)) {
        error_message = "OMS target order is terminal";
        return false;
    }

    if (intent.action == internal::OmsAction::kReplace) {
        if (resolved_target_id == intent.client_order_id) {
            error_message = "OMS replace requires a new client_order_id";
            return false;
        }
        if (in_flight_orders_.contains(intent.client_order_id)) {
            error_message = "OMS replace client_order_id already exists";
            return false;
        }
        if (!intent.market_ticker.empty() && !target_it->second.market_ticker.empty() &&
            intent.market_ticker != target_it->second.market_ticker) {
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

bool OrderManager::apply_update_transition(internal::OrderStateUpdate& update) {
    const auto next_status = to_in_flight_status(update.status);
    if (!next_status.has_value()) {
        transition_reject_count_.fetch_add(1, std::memory_order_relaxed);
        set_error("Unsupported OMS transition status");
        return false;
    }

    std::scoped_lock lock{in_flight_mutex_};
    auto order_it = in_flight_orders_.find(update.client_order_id);
    if (order_it == in_flight_orders_.end() && update.exchange_order_id.has_value()) {
        const auto exchange_it = exchange_to_client_.find(*update.exchange_order_id);
        if (exchange_it != exchange_to_client_.end()) {
            order_it = in_flight_orders_.find(exchange_it->second);
        }
    }

    if (order_it == in_flight_orders_.end()) {
        unknown_order_update_count_.fetch_add(1, std::memory_order_relaxed);
        set_error("Order update does not match an in-flight order");
        return false;
    }

    InFlightOrder& order = order_it->second;
    if (!can_transition(order.status, *next_status)) {
        transition_reject_count_.fetch_add(1, std::memory_order_relaxed);
        set_error("Rejected OMS transition for in-flight order");
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

    transition_applied_count_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

GlobalRiskSnapshot OrderManager::build_risk_snapshot(std::string_view market_ticker,
                                                     std::string_view skip_client_order_id) const {
    GlobalRiskSnapshot snapshot{};
    std::scoped_lock lock{in_flight_mutex_};
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

internal::QtyLots OrderManager::outstanding_qty(const InFlightOrder& order) {
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

void OrderManager::emit_global_risk_reject(const PendingIntent& pending_intent,
                                           const GlobalRiskDecision& decision) {
    internal::OrderStateUpdate update{
        .exchange = pending_intent.intent.exchange,
        .status = internal::OmsOrderStatus::kRejected,
        .client_order_id = pending_intent.intent.client_order_id,
        .exchange_order_id = std::nullopt,
        .market_ticker = pending_intent.intent.market_ticker,
        .recv_ts_ns = monotonic_now_ns(),
        .data =
            internal::OrderReject{
                .client_order_id = pending_intent.intent.client_order_id,
                .exchange_order_id = std::nullopt,
                .reason_code = std::string{GlobalRiskGate::reject_code_name(decision.code)},
                .reason_message = decision.reason_message,
            },
        .raw_payload = {},
    };

    if (!apply_update_transition(update)) {
        update_drop_count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!event_sink_.on_order_update(update)) {
        update_drop_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

void OrderManager::set_error(std::string_view error_message) {
    std::scoped_lock lock{error_mutex_};
    last_error_.assign(error_message);
}

} // namespace trading::oms
