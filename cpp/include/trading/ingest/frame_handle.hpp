#pragma once

#include <cstdint>
#include <type_traits>

namespace trading::ingest {

struct FrameRef {
    uint64_t recv_timestamp_ns{0};
    uint64_t generation{0};
    uint64_t mono_timestamp_ns{0};
    uint32_t slot{0};
    uint32_t payload_size{0};
};
static_assert(std::is_trivially_copyable_v<FrameRef>, "FrameRef must be trivially copyable");

} // namespace trading::ingest
