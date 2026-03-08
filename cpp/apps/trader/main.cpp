#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "trading/adapters/exchanges/kalshi/auth_signer.hpp"
#include "trading/adapters/exchanges/kalshi/ws_adapter.hpp"
#include "trading/adapters/logging/logger.hpp"
#include "trading/adapters/ws/client.hpp"
#include "trading/adapters/ws/feed_runner.hpp"
#include "trading/adapters/ws/session.hpp"
#include "trading/config/trader_config.hpp"
#include "trading/engine/runtime.hpp"
#include "trading/pipeline/live_pipeline.hpp"

namespace {

std::atomic<bool> g_shutdown_requested{false};
constexpr int kExitMissingCredentials = 3;
constexpr int kExitPipelineStartFailure = 4;
constexpr int kExitFeedRunnerStartFailure = 5;
constexpr int kExitConfigLoadFailure = 6;

std::optional<std::string> get_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

void handle_shutdown_signal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        // Signal-only flag: no dependent shared state needs ordering.
        g_shutdown_requested.store(true, std::memory_order_relaxed);
    }
}

std::optional<std::string> resolve_config_path(int argc, char** argv) {
    if (argc >= 3 && std::string_view{argv[1]} == "--config") {
        if (argv[2] != nullptr && !std::string_view{argv[2]}.empty()) {
            return std::string{argv[2]};
        }
    } else if (argc >= 2) {
        if (argv[1] != nullptr && !std::string_view{argv[1]}.empty()) {
            return std::string{argv[1]};
        }
    }
    return get_env("TRADING_CONFIG_PATH");
}

} // namespace

int main(int argc, char** argv) {
    trading::config::TraderRuntimeConfig runtime_config{};
    const auto config_path = resolve_config_path(argc, argv);
    if (config_path.has_value()) {
        const auto loaded = trading::config::load_trader_config_from_file(*config_path);
        if (!loaded.ok) {
            trading::adapters::logging::log_startup("trader.config.error", loaded.error);
            return kExitConfigLoadFailure;
        }
        runtime_config = loaded.config;
        trading::adapters::logging::log_startup("trader.config",
                                                "loaded config file: " + *config_path);
    }

    const auto startup = trading::engine::build_trader_startup_payload(runtime_config.mode);

    const auto key_id = runtime_config.kalshi.credentials.key_id.empty()
                            ? get_env(runtime_config.kalshi.credentials.key_id_env.c_str())
                            : std::optional<std::string>{runtime_config.kalshi.credentials.key_id};
    const auto private_key_pem =
        runtime_config.kalshi.credentials.private_key_pem.empty()
            ? get_env(runtime_config.kalshi.credentials.private_key_pem_env.c_str())
            : std::optional<std::string>{runtime_config.kalshi.credentials.private_key_pem};
    if (!key_id || !private_key_pem) {
        trading::adapters::logging::log_startup("trader",
                                                "missing Kalshi credentials (inline or env)");
        return kExitMissingCredentials;
    }

    trading::adapters::exchanges::kalshi::WsAdapter kalshi_adapter{
        trading::adapters::exchanges::kalshi::AuthSigner{
            trading::adapters::exchanges::kalshi::Credentials{
                .key_id = *key_id,
                .private_key_pem = *private_key_pem,
            },
        },
        runtime_config.kalshi.endpoint,
    };

    trading::pipeline::LivePipeline pipeline{runtime_config.pipeline};
    if (!pipeline.start()) {
        trading::adapters::logging::log_startup("trader.error", "failed to start pipeline");
        return kExitPipelineStartFailure;
    }

    trading::adapters::ws::BoostBeastWsTransport ws_transport;
    trading::adapters::ws::WsSession session{ws_transport, kalshi_adapter};

    trading::adapters::ws::WsFeedRunner feed_runner{
        session,
        pipeline.message_sink(),
        trading::adapters::ws::WsFeedRunnerConfig{
            .channels = runtime_config.kalshi.channels,
        },
    };
    if (!feed_runner.start()) {
        pipeline.stop();
        trading::adapters::logging::log_startup("trader.error", "failed to start ws feed runner");
        return kExitFeedRunnerStartFailure;
    }

    trading::adapters::logging::log_startup("trader", startup);
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    // Poll stop flag only; relaxed is sufficient for this shutdown check.
    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        const std::size_t pumped = pipeline.pump_ingest(runtime_config.pump_batch_size);
        if (pumped == 0) {
            std::this_thread::sleep_for(runtime_config.pump_idle_sleep);
        }
    }

    feed_runner.stop();
    pipeline.stop();

    const auto pipeline_stats = pipeline.stats();
    const auto feed_received = feed_runner.received_count();
    const auto feed_dropped = feed_runner.dropped_count();
    const std::string summary =
        "frames_pumped=" + std::to_string(pipeline_stats.ingest_frames_pumped) +
        ", routed=" + std::to_string(pipeline_stats.route_success) +
        ", route_drop=" + std::to_string(pipeline_stats.route_drop) +
        ", sink_drop=" + std::to_string(pipeline_stats.ingest_sink_dropped) +
        ", shard_drop=" + std::to_string(pipeline_stats.shard_dispatch_dropped) +
        ", ws_received=" + std::to_string(feed_received) +
        ", ws_dropped=" + std::to_string(feed_dropped);
    trading::adapters::logging::log_startup("trader.pipeline", summary);
    return 0;
}
