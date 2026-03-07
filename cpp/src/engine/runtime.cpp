#include "trading/engine/runtime.hpp"

#include "trading/core/app.hpp"

namespace trading::engine {

std::string build_trader_startup_payload(std::string_view mode) {
    return core::build_heartbeat_json(mode);
}

std::string build_logger_startup_payload(std::string_view mode) {
    return core::build_heartbeat_json(mode);
}

} // namespace trading::engine
