#include <spdlog/spdlog.h>

#include "trading/app.hpp"

int main() {
    const auto payload = trading::build_heartbeat_json("dev");
    spdlog::info("startup payload: {}", payload);
    return 0;
}
