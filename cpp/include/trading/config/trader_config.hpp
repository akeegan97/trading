#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "trading/pipeline/live_pipeline.hpp"

namespace trading::config {

enum class TraderExecutionMode : std::uint8_t {
    kLive = 0,
    kPaper = 1,
    kMarketDataOnly = 2,
};

[[nodiscard]] std::string_view execution_mode_name(TraderExecutionMode mode);

struct CredentialConfig {
    std::string key_id;
    std::string private_key_pem;
    std::string key_id_env{"KALSHI_KEY_ID"};
    std::string private_key_pem_env{"KALSHI_PRIVATE_KEY_PEM"};
};

struct KalshiWsConfig {
    std::string endpoint{"wss://api.elections.kalshi.com/trade-api/ws/v2"};
    std::vector<std::string> channels{"trades"};
    CredentialConfig credentials{};
};

struct TraderRuntimeConfig {
    std::string mode{"dev"};
    TraderExecutionMode execution_mode{TraderExecutionMode::kLive};
    KalshiWsConfig kalshi{};
    pipeline::LivePipelineConfig pipeline{};
    std::size_t pump_batch_size{pipeline::LivePipeline::kDefaultPumpBatchSize};
    std::chrono::milliseconds pump_idle_sleep{std::chrono::milliseconds{1}};
};

struct ConfigLoadResult {
    bool ok{false};
    TraderRuntimeConfig config{};
    std::string error;
};

[[nodiscard]] ConfigLoadResult load_trader_config_from_json(std::string_view json_text);
[[nodiscard]] ConfigLoadResult load_trader_config_from_file(const std::string& path);

} // namespace trading::config
