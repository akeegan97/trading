#pragma once

#include <optional>

#include "trading/router/shard_dispatch.hpp"
#include "trading/shards/decoded_message.hpp"

namespace trading::shards {

class IExchangeMessageParser {
  public:
    virtual ~IExchangeMessageParser() = default;
    [[nodiscard]] virtual std::optional<DecodedMessage> parse(
        const router::RoutedEvent& routed_event) const = 0;
};

class HeuristicMessageParser final : public IExchangeMessageParser {
  public:
    [[nodiscard]] std::optional<DecodedMessage> parse(
        const router::RoutedEvent& routed_event) const override;
};

} // namespace trading::shards
