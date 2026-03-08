#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "trading/ingest/frame_handle.hpp"
#include "trading/ingest/raw_frame.hpp"

namespace trading::ingest {

class FramePool {
  public:
    explicit FramePool(std::size_t capacity);

    // Acquires a writable frame slot. Returns false when the pool is exhausted.
    [[nodiscard]] bool acquire(FrameRef& ref_out) noexcept;
    // Releases a slot after the consumer is done reading that frame.
    [[nodiscard]] bool release(const FrameRef& ref) noexcept;

    [[nodiscard]] RawFrame* frame(const FrameRef& ref) noexcept;
    [[nodiscard]] const RawFrame* frame(const FrameRef& ref) const noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t available() const noexcept;

  private:
    static constexpr uint32_t kPayloadSizeUnset = 0;
    static constexpr uint64_t kInitialGeneration = 1;

    uint32_t slot_count_;
    mutable std::mutex mutex_;
    std::vector<RawFrame> slots_;
    std::vector<uint64_t> generations_;
    std::vector<uint8_t> in_use_;
    std::vector<uint32_t> free_slots_;
};

} // namespace trading::ingest
