#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "trading/internal/oms_types.hpp"
#include "trading/oms/exchange_adapter.hpp"
#include "trading/oms/order_event_sink.hpp"
#include "trading/oms/transport.hpp"

namespace trading::oms {

struct OrderManagerConfig {
    static constexpr auto kDefaultLoopIdleSleep = std::chrono::milliseconds{1};

    OrderTransportConfig transport;
    std::chrono::milliseconds loop_idle_sleep{kDefaultLoopIdleSleep};
};

struct OrderManagerStats {
    std::uint64_t submitted_count{0};
    std::uint64_t sent_count{0};
    std::uint64_t send_failed_count{0};
    std::uint64_t receive_count{0};
    std::uint64_t parse_failed_count{0};
    std::uint64_t update_drop_count{0};
    std::uint64_t unsupported_intent_count{0};
    std::size_t pending_intent_count{0};
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

  private:
    struct PendingIntent {
        internal::OrderRequestId request_id{0};
        internal::OrderIntent intent;
    };

    static internal::TimestampNs monotonic_now_ns();
    void run(const std::stop_token& stop_token);
    [[nodiscard]] std::size_t drain_outbound_intents();
    [[nodiscard]] bool pump_incoming_update();
    void set_error(std::string_view error_message);

    const IExchangeOmsAdapter& adapter_;
    IOrderTransport& transport_;
    IOrderEventSink& event_sink_;
    OrderManagerConfig config_;

    std::jthread worker_;
    mutable std::mutex queue_mutex_;
    std::deque<PendingIntent> outbound_queue_;

    mutable std::mutex error_mutex_;
    std::string last_error_;

    std::atomic<bool> running_{false};
    std::atomic<internal::OrderRequestId> next_request_id_{1};
    std::atomic<std::uint64_t> submitted_count_{0};
    std::atomic<std::uint64_t> sent_count_{0};
    std::atomic<std::uint64_t> send_failed_count_{0};
    std::atomic<std::uint64_t> receive_count_{0};
    std::atomic<std::uint64_t> parse_failed_count_{0};
    std::atomic<std::uint64_t> update_drop_count_{0};
    std::atomic<std::uint64_t> unsupported_intent_count_{0};
};

} // namespace trading::oms
