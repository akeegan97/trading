#include "trading/adapters/logging/logger.hpp"

#include <spdlog/spdlog.h>

namespace trading::adapters::logging {

void log_startup(std::string_view component, std::string_view payload) {
    spdlog::info("{} startup payload: {}", component, payload);
}

} // namespace trading::adapters::logging
