#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "trading/internal/oms_types.hpp"
#include "trading/oms/exchange_adapter.hpp"
#include "trading/oms/global_risk_gate.hpp"
#include "trading/oms/order_event_sink.hpp"
#include "trading/oms/transport.hpp"

namespace trading::oms {

struct OrderManagerConfig {
    static constexpr auto kDefaultLoopIdleSleep = std::chrono::milliseconds{1};

    OrderTransportConfig transport;
    GlobalRiskConfig global_risk{};
    std::chrono::milliseconds loop_idle_sleep{kDefaultLoopIdleSleep};
};

struct OrderManagerStats {
    std::uint64_t submitted_count{0};
    std::uint64_t sent_count{0};
    std::uint64_t send_failed_count{0};
    std::uint64_t risk_reject_count{0};
    std::uint64_t receive_count{0};
    std::uint64_t parse_failed_count{0};
    std::uint64_t update_drop_count{0};
    std::uint64_t transition_applied_count{0};
    std::uint64_t transition_reject_count{0};
    std::uint64_t unknown_order_update_count{0};
    std::uint64_t policy_reject_count{0};
    std::uint64_t unsupported_intent_count{0};
    std::size_t pending_intent_count{0};
    std::size_t tracked_order_count{0};
    std::size_t active_order_count{0};
};

enum class InFlightStatus : std::uint8_t {
    kPending = 0,
    kAccepted = 1,
    kPartiallyFilled = 2,
    kFilled = 3,
    kCanceled = 4,
    kRejected = 5,
    kReplaced = 6,
};

struct InFlightOrderSnapshot {
    internal::OrderRequestId request_id{0};
    internal::OmsAction last_action{internal::OmsAction::kUnknown};
    InFlightStatus status{InFlightStatus::kPending};
    internal::ClientOrderId client_order_id;
    std::optional<internal::ExchangeOrderId> exchange_order_id;
    std::optional<internal::ClientOrderId> replace_target_client_order_id;
    std::string market_ticker;
    internal::QtyLots requested_qty_lots{0};
    internal::QtyLots filled_qty_lots{0};
    internal::TimestampNs created_ts_ns{0};
    internal::TimestampNs last_update_ts_ns{0};
};

class OrderManager final {
  public:
    OrderManager(const IExchangeOmsAdapter& adapter, IOrderTransport& transport,
                 IOrderEventSink& event_sink, OrderManagerConfig config = {});
    ~OrderManager();

    OrderManager(const OrderManager&) = delete;
    OrderManager& operator=(const OrderManager&) = delete;
    OrderManager(OrderManager&&) = delete;
    OrderManager& operator=(OrderManager&&) = delete;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool running() const;

    [[nodiscard]] std::optional<internal::OrderRequestId> submit(internal::OrderIntent intent);
    [[nodiscard]] OrderManagerStats stats() const;
    [[nodiscard]] std::string last_error() const;
    [[nodiscard]] std::optional<InFlightOrderSnapshot>
    in_flight_order(std::string_view client_order_id) const;

  private:
    struct PendingIntent {
        internal::OrderRequestId request_id{0};
        internal::OrderIntent intent;
    };

    struct InFlightOrder {
        internal::OrderRequestId request_id{0};
        internal::OmsAction last_action{internal::OmsAction::kUnknown};
        InFlightStatus status{InFlightStatus::kPending};
        internal::ClientOrderId client_order_id;
        std::optional<internal::ExchangeOrderId> exchange_order_id;
        std::optional<internal::ClientOrderId> replace_target_client_order_id;
        std::string market_ticker;
        internal::QtyLots requested_qty_lots{0};
        internal::QtyLots filled_qty_lots{0};
        internal::TimestampNs created_ts_ns{0};
        internal::TimestampNs last_update_ts_ns{0};
    };

    static internal::TimestampNs monotonic_now_ns();
    [[nodiscard]] static bool is_terminal_state(InFlightStatus status);
    [[nodiscard]] static std::optional<InFlightStatus>
    to_in_flight_status(internal::OmsOrderStatus status);
    [[nodiscard]] static bool can_transition(InFlightStatus current, InFlightStatus next);

    void run(const std::stop_token& stop_token);
    [[nodiscard]] std::size_t drain_outbound_intents();
    [[nodiscard]] bool pump_incoming_update();
    [[nodiscard]] bool validate_and_track_submission(internal::OrderRequestId request_id,
                                                     const internal::OrderIntent& intent,
                                                     std::string& error_message);
    [[nodiscard]] bool apply_update_transition(const internal::OrderStateUpdate& update);
    [[nodiscard]] GlobalRiskSnapshot
    build_risk_snapshot(std::string_view market_ticker,
                        std::string_view skip_client_order_id) const;
    [[nodiscard]] static internal::QtyLots outstanding_qty(const InFlightOrder& order);
    void emit_global_risk_reject(const PendingIntent& pending_intent,
                                 const GlobalRiskDecision& decision);
    void set_error(std::string_view error_message);

    const IExchangeOmsAdapter& adapter_;
    IOrderTransport& transport_;
    IOrderEventSink& event_sink_;
    OrderManagerConfig config_;
    GlobalRiskGate global_risk_gate_;

    std::jthread worker_;
    mutable std::mutex queue_mutex_;
    std::deque<PendingIntent> outbound_queue_;

    mutable std::mutex in_flight_mutex_;
    std::unordered_map<internal::ClientOrderId, InFlightOrder> in_flight_orders_;
    std::unordered_map<internal::ExchangeOrderId, internal::ClientOrderId> exchange_to_client_;

    mutable std::mutex error_mutex_;
    std::string last_error_;

    std::atomic<bool> running_{false};
    std::atomic<internal::OrderRequestId> next_request_id_{1};
    std::atomic<std::uint64_t> submitted_count_{0};
    std::atomic<std::uint64_t> sent_count_{0};
    std::atomic<std::uint64_t> send_failed_count_{0};
    std::atomic<std::uint64_t> risk_reject_count_{0};
    std::atomic<std::uint64_t> receive_count_{0};
    std::atomic<std::uint64_t> parse_failed_count_{0};
    std::atomic<std::uint64_t> update_drop_count_{0};
    std::atomic<std::uint64_t> transition_applied_count_{0};
    std::atomic<std::uint64_t> transition_reject_count_{0};
    std::atomic<std::uint64_t> unknown_order_update_count_{0};
    std::atomic<std::uint64_t> policy_reject_count_{0};
    std::atomic<std::uint64_t> unsupported_intent_count_{0};
};

} // namespace trading::oms
