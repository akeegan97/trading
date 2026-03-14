#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "trading/oms/transport.hpp"

namespace trading::oms {

struct PaperOrderTransportConfig {
    bool auto_fill_on_place{true};
};

class PaperOrderTransport final : public IOrderTransport {
  public:
    explicit PaperOrderTransport(PaperOrderTransportConfig config = {});

    [[nodiscard]] bool connect(const OrderTransportConfig& config) override;
    [[nodiscard]] bool send_text(std::string_view payload) override;
    [[nodiscard]] std::optional<std::string> recv_text() override;
    void close() override;
    [[nodiscard]] std::string_view last_error() const override;

  private:
    enum class ErrorCode : std::uint8_t {
        kNone = 0,
        kNotConnected,
        kInvalidPayload,
        kInvalidRequest,
        kUnknownAction,
    };

    void set_error(ErrorCode error_code);

    PaperOrderTransportConfig config_;
    mutable std::mutex mutex_;
    bool connected_{false};
    std::uint64_t next_recv_ts_ns_{1};
    std::uint64_t next_exchange_order_id_{1};
    std::deque<std::string> inbound_updates_;
    std::atomic<ErrorCode> last_error_code_{ErrorCode::kNone};
};

} // namespace trading::oms
