#include <cstdlib>
#include <optional>
#include <string>

#include "trading/adapters/exchanges/kalshi/auth_signer.hpp"
#include "trading/adapters/exchanges/kalshi/ws_adapter.hpp"
#include "trading/adapters/logging/logger.hpp"
#include "trading/adapters/ws/client.hpp"
#include "trading/adapters/ws/session.hpp"
#include "trading/engine/runtime.hpp"

namespace {

std::optional<std::string> get_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

} // namespace

int main() {
    const auto startup = trading::engine::build_trader_startup_payload("dev");

    const auto key_id = get_env("KALSHI_KEY_ID");
    const auto private_key_pem = get_env("KALSHI_PRIVATE_KEY_PEM");
    if (!key_id || !private_key_pem) {
        trading::adapters::logging::log_startup("trader",
                                                "missing KALSHI_KEY_ID or KALSHI_PRIVATE_KEY_PEM");
        return 3;
    }

    trading::adapters::exchanges::kalshi::WsAdapter kalshi_adapter{
        trading::adapters::exchanges::kalshi::AuthSigner{
            trading::adapters::exchanges::kalshi::Credentials{
                .key_id = *key_id,
                .private_key_pem = *private_key_pem,
            },
        },
    };
    trading::adapters::ws::BoostBeastWsTransport ws_transport;
    trading::adapters::ws::WsSession session{ws_transport, kalshi_adapter};
    if (!session.connect()) {
        trading::adapters::logging::log_startup("trader.error", ws_transport.last_error());
        return 1;
    }
    if (!session.subscribe("trades")) {
        trading::adapters::logging::log_startup("trader.error", ws_transport.last_error());
        return 2;
    }

    trading::adapters::logging::log_startup("trader", startup);
    trading::adapters::logging::log_startup("trader.ws", session.last_subscribe_payload());
    return 0;
}
