#pragma once

#include <map>
#include <string>
#include <string_view>

namespace trading::adapters::exchanges {

struct ConnectRequest {
    std::string endpoint;
    std::map<std::string, std::string> headers;
};

class IExchangeWsAdapter {
  public:
    virtual ~IExchangeWsAdapter() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    // Builds exchange-specific handshake details (endpoint + auth headers).
    [[nodiscard]] virtual ConnectRequest build_connect_request() const = 0;
    // Builds exchange-specific wire payloads for subscriptions.
    [[nodiscard]] virtual std::string build_subscribe_message(std::string_view channel) const = 0;
};

} // namespace trading::adapters::exchanges
