#include "trading/ingest/ws_message_sink.hpp"

#include <chrono>
#include <utility>

namespace trading::ingest {

FramePoolMessageSink::FramePoolMessageSink(FramePool& pool,
                                           SpscFrameQueue& queue,
                                           std::string source)
    : pool_(pool), queue_(queue), source_(std::move(source)) {}

bool FramePoolMessageSink::push_message(std::string message) {
    FrameRef ref{};
    if (!pool_.acquire(ref)) {
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    RawFrame* frame = pool_.frame(ref);
    if (frame == nullptr) {
        (void)pool_.release(ref);
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    frame->recv_timestamp = std::chrono::steady_clock::now();
    frame->source = source_;
    frame->payload = std::move(message);
    frame->market_ticker.clear();
    frame->seq_id.reset();
    ref.payload_size = static_cast<uint32_t>(frame->payload.size());

    if (!queue_.try_push(ref)) {
        (void)pool_.release(ref);
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    return true;
}

std::size_t FramePoolMessageSink::dropped_count() const {
    return dropped_count_.load(std::memory_order_relaxed);
}

} // namespace trading::ingest
