#include "trading/shards/shard.hpp"

#include <thread>

namespace trading::shards {

Shard::Shard(ShardConfig config,
             router::ShardedEventDispatch& dispatch,
             IExchangeMessageParser& parser,
             BookStore& books)
    : config_(config), dispatch_(dispatch), parser_(parser), books_(books) {}

Shard::~Shard() { stop(); }

bool Shard::start() {
    if (worker_.joinable()) {
        return false;
    }

    consumed_.store(0, std::memory_order_relaxed);
    parsed_.store(0, std::memory_order_relaxed);
    parse_errors_.store(0, std::memory_order_relaxed);
    applied_.store(0, std::memory_order_relaxed);
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

bool Shard::running() const { return running_.load(std::memory_order_acquire); }

ShardStats Shard::stats() const {
    return ShardStats{
        .consumed = consumed_.load(std::memory_order_relaxed),
        .parsed = parsed_.load(std::memory_order_relaxed),
        .parse_errors = parse_errors_.load(std::memory_order_relaxed),
        .applied = applied_.load(std::memory_order_relaxed),
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
        auto decoded = parser_.parse(routed);
        if (!decoded) {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        parsed_.fetch_add(1, std::memory_order_relaxed);
        if (books_.apply(*decoded)) {
            applied_.fetch_add(1, std::memory_order_relaxed);
        } else {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    running_.store(false, std::memory_order_release);
}

} // namespace trading::shards
