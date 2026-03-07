#include "trading/core/app.hpp"

#include <nlohmann/json.hpp>

namespace trading::core {

std::string build_heartbeat_json(std::string_view mode) {
    const nlohmann::json payload{
        {"service", "trading_engine"},
        {"status", "ok"},
        {"mode", mode},
    };
    return payload.dump();
}

} // namespace trading::core
