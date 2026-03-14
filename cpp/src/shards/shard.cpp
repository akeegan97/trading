#include "trading/shards/shard.hpp"

#include <thread>

namespace trading::shards {

Shard::Shard(ShardConfig config, router::ShardedEventDispatch& dispatch,
             IExchangeMessageParser& parser, BookStore& books, IShardEventHandler* event_handler)
    : config_(config), dispatch_(dispatch), parser_(parser), books_(books),
      event_handler_(event_handler) {}

Shard::~Shard() { stop(); }

bool Shard::start() {
    if (worker_.joinable()) {
        return false;
    }

    consumed_.store(0, std::memory_order_relaxed);
    parsed_.store(0, std::memory_order_relaxed);
    parse_errors_.store(0, std::memory_order_relaxed);
    applied_.store(0, std::memory_order_relaxed);
    handler_invoked_.store(0, std::memory_order_relaxed);
    handler_errors_.store(0, std::memory_order_relaxed);
    // Publish started state to readers of running().
    running_.store(true, std::memory_order_release);

    worker_ = std::jthread([this](const std::stop_token& stop_token) { run(stop_token); });
    return true;
}

void Shard::stop() {
    if (!worker_.joinable()) {
        running_.store(false, std::memory_order_release);
        return;
    }

    worker_.request_stop();
    worker_.join();
    running_.store(false, std::memory_order_release);
}

// Acquire pairs with start/stop release stores.
bool Shard::running() const { return running_.load(std::memory_order_acquire); }

ShardStats Shard::stats() const {
    return ShardStats{
        .consumed = consumed_.load(std::memory_order_relaxed),
        .parsed = parsed_.load(std::memory_order_relaxed),
        .parse_errors = parse_errors_.load(std::memory_order_relaxed),
        .applied = applied_.load(std::memory_order_relaxed),
        .handler_invoked = handler_invoked_.load(std::memory_order_relaxed),
        .handler_errors = handler_errors_.load(std::memory_order_relaxed),
    };
}

void Shard::run(const std::stop_token& stop_token) {
    while (!stop_token.stop_requested()) {
        router::RoutedEvent routed{};
        if (!dispatch_.try_pop(config_.shard_id, routed)) {
            std::this_thread::sleep_for(config_.idle_sleep);
            continue;
        }

        consumed_.fetch_add(1, std::memory_order_relaxed);
        const auto parsed = parser_.parse(routed);
        if (!parsed.ok()) {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const auto& parsed_event = parsed.value();
        if (!parsed_event.has_value()) {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        parsed_.fetch_add(1, std::memory_order_relaxed);
        if (books_.apply(*parsed_event)) {
            applied_.fetch_add(1, std::memory_order_relaxed);
            if (event_handler_ != nullptr) {
                handler_invoked_.fetch_add(1, std::memory_order_relaxed);
                if (!event_handler_->on_event(*parsed_event)) {
                    handler_errors_.fetch_add(1, std::memory_order_relaxed);
                }
            }
        } else {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    running_.store(false, std::memory_order_release);
}

} // namespace trading::shards
