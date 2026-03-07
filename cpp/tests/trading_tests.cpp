#include <nlohmann/json.hpp>

#include "trading/app.hpp"

namespace {

int run_tests() {
    const auto payload = trading::build_heartbeat_json("test");
    const auto parsed = nlohmann::json::parse(payload);

    if (!parsed.contains("service") || parsed["service"] != "trading_engine") {
        return 1;
    }
    if (!parsed.contains("status") || parsed["status"] != "ok") {
        return 1;
    }
    if (!parsed.contains("mode") || parsed["mode"] != "test") {
        return 1;
    }

    return 0;
}

} // namespace

int main() {
    try {
        return run_tests();
    } catch (...) {
        return 2;
    }
}
