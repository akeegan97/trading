#include "trading/pipeline/live_pipeline.hpp"

#include <utility>

namespace trading::pipeline {

LivePipeline::LivePipeline(LivePipelineConfig config)
    : config_(std::move(config)), frame_pool_(config_.frame_pool_capacity),
      frame_queue_(config_.frame_queue_capacity), sink_(frame_pool_, frame_queue_, config_.source),
      shard_dispatch_(router::ShardedEventDispatchConfig{
          .shard_count = config_.shard_count,
          .per_shard_queue_capacity = config_.per_shard_queue_capacity,
      }),
      router_(decode::resolve_decoder(config_.decode_exchange), shard_dispatch_,
              shard_dispatch_.shard_count(), config_.router_exchange) {
    const std::size_t resolved_shard_count = shard_dispatch_.shard_count();
    book_stores_.reserve(resolved_shard_count);
    shard_event_handlers_.reserve(resolved_shard_count);
    shards_.reserve(resolved_shard_count);
    for (std::size_t shard_id = 0; shard_id < resolved_shard_count; ++shard_id) {
        std::unique_ptr<shards::IShardEventHandler> event_handler{};
        if (config_.shard_event_handler_factory) {
            event_handler = config_.shard_event_handler_factory(shard_id);
        }
        book_stores_.push_back(std::make_unique<shards::BookStore>());
        shard_event_handlers_.push_back(std::move(event_handler));
        shards_.push_back(std::make_unique<shards::Shard>(
            shards::ShardConfig{
                .shard_id = shard_id,
                .idle_sleep = config_.shard_idle_sleep,
            },
            shard_dispatch_, parser_, *book_stores_.back(), shard_event_handlers_.back().get()));
    }
}

LivePipeline::~LivePipeline() { stop(); }

bool LivePipeline::start() {
    // Acquire pairs with stop/start release stores.
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }

    for (std::size_t i = 0; i < shards_.size(); ++i) {
        if (shards_[i] != nullptr && !shards_[i]->start()) {
            for (std::size_t started = 0; started < i; ++started) {
                if (shards_[started] != nullptr) {
                    shards_[started]->stop();
                }
            }
            return false;
        }
    }

    ingest_frames_pumped_.store(0, std::memory_order_relaxed);
    route_success_.store(0, std::memory_order_relaxed);
    route_drop_.store(0, std::memory_order_relaxed);
    frame_release_failures_.store(0, std::memory_order_relaxed);
    // Publish started state and initialized counters.
    running_.store(true, std::memory_order_release);
    return true;
}

void LivePipeline::stop() {
    // Acquire pairs with start/stop release stores.
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    for (auto& shard : shards_) {
        if (shard != nullptr) {
            shard->stop();
        }
    }
    running_.store(false, std::memory_order_release);
}

// Acquire pairs with start/stop release stores.
bool LivePipeline::running() const { return running_.load(std::memory_order_acquire); }

adapters::ws::IWsMessageSink& LivePipeline::message_sink() { return sink_; }

std::size_t LivePipeline::pump_ingest(std::size_t max_frames) noexcept {
    if (max_frames == 0) {
        return 0;
    }

    std::size_t popped = 0;
    std::size_t route_success = 0;
    std::size_t route_drop = 0;
    std::size_t release_failures = 0;

    ingest::FrameRef ref{};
    while (popped < max_frames && frame_queue_.try_pop(ref)) {
        ++popped;
        const ingest::RawFrame* frame = frame_pool_.frame(ref);
        if (frame != nullptr) {
            if (router_.route(*frame)) {
                ++route_success;
            } else {
                ++route_drop;
            }
        } else {
            ++route_drop;
        }

        if (!frame_pool_.release(ref)) {
            ++release_failures;
        }
    }

    ingest_frames_pumped_.fetch_add(popped, std::memory_order_relaxed);
    route_success_.fetch_add(route_success, std::memory_order_relaxed);
    route_drop_.fetch_add(route_drop, std::memory_order_relaxed);
    frame_release_failures_.fetch_add(release_failures, std::memory_order_relaxed);
    return popped;
}

LivePipelineStats LivePipeline::stats() const {
    return LivePipelineStats{
        .ingest_frames_pumped = ingest_frames_pumped_.load(std::memory_order_relaxed),
        .route_success = route_success_.load(std::memory_order_relaxed),
        .route_drop = route_drop_.load(std::memory_order_relaxed),
        .frame_release_failures = frame_release_failures_.load(std::memory_order_relaxed),
        .ingest_sink_dropped = sink_.dropped_count(),
        .shard_dispatch_dropped = shard_dispatch_.dropped_count(),
    };
}

std::size_t LivePipeline::shard_count() const { return shards_.size(); }

const shards::BookStore* LivePipeline::book_store(std::size_t shard_id) const {
    if (shard_id >= book_stores_.size() || book_stores_[shard_id] == nullptr) {
        return nullptr;
    }
    return book_stores_[shard_id].get();
}

} // namespace trading::pipeline
