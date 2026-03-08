#include "trading/ingest/frame_pool.hpp"

#include <chrono>

namespace trading::ingest {
namespace {

uint64_t now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace

FramePool::FramePool(std::size_t capacity)
    : slot_count_(static_cast<uint32_t>(capacity)),
      slots_(capacity),
      generations_(capacity, kInitialGeneration),
      in_use_(capacity, 0),
      free_slots_(capacity) {
    for (uint32_t i = 0; i < slot_count_; ++i) {
        free_slots_[i] = slot_count_ - i - 1;
    }
}

bool FramePool::acquire(FrameRef& ref_out) noexcept {
    std::scoped_lock lock{mutex_};
    if (free_slots_.empty()) {
        return false;
    }

    const uint32_t slot = free_slots_.back();
    free_slots_.pop_back();
    in_use_[slot] = 1;
    slots_[slot] = RawFrame{};
    if (generations_[slot] == 0) {
        generations_[slot] = kInitialGeneration;
    }

    const uint64_t timestamp_ns = now_ns();
    ref_out.slot = slot;
    ref_out.generation = generations_[slot];
    ref_out.payload_size = kPayloadSizeUnset;
    ref_out.recv_timestamp_ns = timestamp_ns;
    ref_out.mono_timestamp_ns = timestamp_ns;
    return true;
}

bool FramePool::release(const FrameRef& ref) noexcept {
    std::scoped_lock lock{mutex_};
    if (ref.slot >= slot_count_) {
        return false;
    }
    if (in_use_[ref.slot] == 0) {
        return false;
    }
    if (ref.generation != generations_[ref.slot]) {
        return false;
    }

    in_use_[ref.slot] = 0;
    ++generations_[ref.slot];
    if (generations_[ref.slot] == 0) {
        generations_[ref.slot] = kInitialGeneration;
    }
    free_slots_.push_back(ref.slot);
    return true;
}

RawFrame* FramePool::frame(const FrameRef& ref) noexcept {
    std::scoped_lock lock{mutex_};
    if (ref.slot >= slot_count_) {
        return nullptr;
    }
    if (in_use_[ref.slot] == 0) {
        return nullptr;
    }
    if (ref.generation != generations_[ref.slot]) {
        return nullptr;
    }
    return &slots_[ref.slot];
}

const RawFrame* FramePool::frame(const FrameRef& ref) const noexcept {
    std::scoped_lock lock{mutex_};
    if (ref.slot >= slot_count_) {
        return nullptr;
    }
    if (in_use_[ref.slot] == 0) {
        return nullptr;
    }
    if (ref.generation != generations_[ref.slot]) {
        return nullptr;
    }
    return &slots_[ref.slot];
}

std::size_t FramePool::capacity() const noexcept { return slots_.size(); }

std::size_t FramePool::available() const noexcept {
    std::scoped_lock lock{mutex_};
    return free_slots_.size();
}

} // namespace trading::ingest
