#pragma once

#include <atomic>
#include <string>

#include "trading/adapters/ws/feed_runner.hpp"
#include "trading/ingest/frame_pool.hpp"
#include "trading/ingest/spsc_frame_queue.hpp"

namespace trading::ingest {

class FramePoolMessageSink final : public adapters::ws::IWsMessageSink {
  public:
    FramePoolMessageSink(FramePool& pool, SpscFrameQueue& queue, std::string source);

    bool push_message(std::string message) override;
    [[nodiscard]] std::size_t dropped_count() const;

  private:
    FramePool& pool_;
    SpscFrameQueue& queue_;
    std::string source_;
    std::atomic<std::size_t> dropped_count_{0};
};

} // namespace trading::ingest
