#include "trading/app.hpp"

#include <string_view>

#include <nlohmann/json.hpp>

namespace trading {

std::string build_heartbeat_json(std::string_view mode) {
    const nlohmann::json payload{
        {"service", "trading_engine"},
        {"status", "ok"},
        {"mode", mode},
    };
    return payload.dump();
}

} // namespace trading
