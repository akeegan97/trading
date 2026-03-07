#include "trading/adapters/logging/logger.hpp"
#include "trading/engine/runtime.hpp"

int main() {
    const auto startup = trading::engine::build_logger_startup_payload("dev");
    trading::adapters::logging::log_startup("logger", startup);
    return 0;
}
