#include "trading/oms/ws_order_transport.hpp"

#include <utility>

namespace trading::oms {

WsOrderTransport::WsOrderTransport(adapters::ws::IWsTransport& ws_transport)
    : ws_transport_(ws_transport) {}

bool WsOrderTransport::connect(const OrderTransportConfig& config) {
    const adapters::ws::TransportConfig ws_config{
        .endpoint = config.endpoint,
        .headers = config.headers,
    };
    return ws_transport_.connect(ws_config);
}

bool WsOrderTransport::send_text(std::string_view payload) {
    return ws_transport_.send_text(payload);
}

std::optional<std::string> WsOrderTransport::recv_text() { return ws_transport_.recv_text(); }

void WsOrderTransport::close() { ws_transport_.close(); }

std::string_view WsOrderTransport::last_error() const { return ws_transport_.last_error(); }

} // namespace trading::oms
