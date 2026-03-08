#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "trading/model/normalized_event.hpp"
#include "trading/router/route_key.hpp"

namespace trading::router {

class IShardDispatch {
  public:
    virtual ~IShardDispatch() = default;

    virtual bool dispatch(const RouteKey& route_key, const model::NormalizedEvent& event) = 0;
};

struct RoutedEvent {
    RouteKey route_key;
    model::NormalizedEvent event;
};

class SpscRoutedEventQueue {
  public:
    explicit SpscRoutedEventQueue(std::size_t capacity);

    [[nodiscard]] bool try_push(const RoutedEvent& event) noexcept;
    [[nodiscard]] bool try_pop(RoutedEvent& event_out) noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

  private:
    [[nodiscard]] std::size_t increment(std::size_t index) const noexcept;

    std::vector<RoutedEvent> buffer_;
    std::size_t capacity_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

struct ShardedEventDispatchConfig {
    static constexpr std::size_t kDefaultPerShardQueueCapacity = 1024;

    std::size_t shard_count{1};
    std::size_t per_shard_queue_capacity{kDefaultPerShardQueueCapacity};
};

class ShardedEventDispatch final : public IShardDispatch {
  public:
    explicit ShardedEventDispatch(ShardedEventDispatchConfig config);

    bool dispatch(const RouteKey& route_key, const model::NormalizedEvent& event) override;
    [[nodiscard]] bool try_pop(std::size_t shard_id, RoutedEvent& event_out);
    [[nodiscard]] std::size_t shard_count() const;
    [[nodiscard]] std::size_t dropped_count() const;

  private:
    std::vector<std::unique_ptr<SpscRoutedEventQueue>> shard_queues_;
    std::atomic<std::size_t> dropped_count_{0};
};

class NoopShardDispatch final : public IShardDispatch {
  public:
    bool dispatch(const RouteKey& route_key, const model::NormalizedEvent& event) override;
    [[nodiscard]] std::size_t dispatched_count() const;

  private:
    std::atomic<std::size_t> dispatched_count_{0};
};

} // namespace trading::router
