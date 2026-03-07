#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "trading/adapters/ws/client.hpp"

namespace trading::tests {

class FakeWsTransport final : public adapters::ws::IWsTransport {
  public:
    bool connect(const adapters::ws::TransportConfig& config) override {
        config_ = config;
        connected_ = true;
        return true;
    }

    bool send_text(std::string_view payload) override {
        if (!connected_) {
            return false;
        }
        sent_messages_.emplace_back(payload);
        return true;
    }

    std::optional<std::string> recv_text() override { return std::nullopt; }

    void close() override { connected_ = false; }

    [[nodiscard]] bool connected() const { return connected_; }
    [[nodiscard]] const adapters::ws::TransportConfig& config() const { return config_; }
    [[nodiscard]] const std::vector<std::string>& sent_messages() const { return sent_messages_; }

  private:
    bool connected_{false};
    adapters::ws::TransportConfig config_{};
    std::vector<std::string> sent_messages_;
};

} // namespace trading::tests
