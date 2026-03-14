#include "trading/oms/paper_order_transport.hpp"

#include <utility>

#include <nlohmann/json.hpp>

namespace trading::oms {
namespace {

using nlohmann::json;

std::optional<std::string> try_get_string(const json& object, std::string_view key) {
    const auto field_it = object.find(key);
    if (field_it == object.end() || !field_it->is_string()) {
        return std::nullopt;
    }
    return field_it->get<std::string>();
}

std::optional<std::int64_t> try_get_int64(const json& object, std::string_view key) {
    const auto field_it = object.find(key);
    if (field_it == object.end() || !field_it->is_number_integer()) {
        return std::nullopt;
    }
    return field_it->get<std::int64_t>();
}

} // namespace

PaperOrderTransport::PaperOrderTransport(PaperOrderTransportConfig config)
    : config_(config) {}

bool PaperOrderTransport::connect(const OrderTransportConfig& config) {
    static_cast<void>(config);
    std::scoped_lock lock{mutex_};
    connected_ = true;
    last_error_code_.store(ErrorCode::kNone, std::memory_order_relaxed);
    return true;
}

bool PaperOrderTransport::send_text(std::string_view payload) {
    std::scoped_lock lock{mutex_};
    if (!connected_) {
        set_error(ErrorCode::kNotConnected);
        return false;
    }

    json request;
    try {
        request = json::parse(payload);
    } catch (...) {
        set_error(ErrorCode::kInvalidPayload);
        return false;
    }

    if (!request.is_object()) {
        set_error(ErrorCode::kInvalidPayload);
        return false;
    }

    const auto action = try_get_string(request, "action");
    const auto client_order_id = try_get_string(request, "client_order_id");
    if (!action.has_value() || !client_order_id.has_value() || client_order_id->empty()) {
        set_error(ErrorCode::kInvalidRequest);
        return false;
    }

    if (*action == "place_order") {
        const auto market_ticker = try_get_string(request, "market_ticker");
        const auto qty = try_get_int64(request, "qty");
        const auto limit_price = try_get_int64(request, "limit_price");
        const auto side = try_get_string(request, "side");
        if (!market_ticker.has_value() || !qty.has_value() || !limit_price.has_value() ||
            !side.has_value() || *qty <= 0) {
            set_error(ErrorCode::kInvalidRequest);
            return false;
        }

        const std::string exchange_order_id =
            "paper-ex-" + std::to_string(next_exchange_order_id_++);
        inbound_updates_.push_back(json{
                                      {"type", "order_ack"},
                                      {"recv_ts_ns", next_recv_ts_ns_++},
                                      {"msg",
                                       {
                                           {"client_order_id", *client_order_id},
                                           {"exchange_order_id", exchange_order_id},
                                           {"market_ticker", *market_ticker},
                                           {"accepted_qty", *qty},
                                       }},
                                  }
                                      .dump());

        if (config_.auto_fill_on_place) {
            inbound_updates_.push_back(json{
                                          {"type", "fill"},
                                          {"recv_ts_ns", next_recv_ts_ns_++},
                                          {"msg",
                                           {
                                               {"client_order_id", *client_order_id},
                                               {"exchange_order_id", exchange_order_id},
                                               {"market_ticker", *market_ticker},
                                               {"fill_qty", *qty},
                                               {"fill_price", *limit_price},
                                               {"side", *side},
                                           }},
                                      }
                                          .dump());
        }

        last_error_code_.store(ErrorCode::kNone, std::memory_order_relaxed);
        return true;
    }

    if (*action == "cancel_order" || *action == "replace_order") {
        const auto market_ticker = try_get_string(request, "market_ticker");
        inbound_updates_.push_back(json{
                                      {"type", "order_reject"},
                                      {"recv_ts_ns", next_recv_ts_ns_++},
                                      {"msg",
                                       {
                                           {"client_order_id", *client_order_id},
                                           {"market_ticker",
                                            market_ticker.value_or(std::string{"paper"})},
                                           {"reason_code", "paper_action_unsupported"},
                                           {"reason_message",
                                            "paper transport currently supports place_order only"},
                                       }},
                                  }
                                      .dump());
        last_error_code_.store(ErrorCode::kNone, std::memory_order_relaxed);
        return true;
    }

    set_error(ErrorCode::kUnknownAction);
    return false;
}

std::optional<std::string> PaperOrderTransport::recv_text() {
    std::scoped_lock lock{mutex_};
    if (!connected_ || inbound_updates_.empty()) {
        return std::nullopt;
    }
    std::string payload = std::move(inbound_updates_.front());
    inbound_updates_.pop_front();
    return payload;
}

void PaperOrderTransport::close() {
    std::scoped_lock lock{mutex_};
    connected_ = false;
}

std::string_view PaperOrderTransport::last_error() const {
    switch (last_error_code_.load(std::memory_order_relaxed)) {
    case ErrorCode::kNone:
        return "";
    case ErrorCode::kNotConnected:
        return "paper transport not connected";
    case ErrorCode::kInvalidPayload:
        return "paper transport invalid request payload";
    case ErrorCode::kInvalidRequest:
        return "paper transport invalid request";
    case ErrorCode::kUnknownAction:
        return "paper transport unknown action";
    }
    return "paper transport unknown error";
}

void PaperOrderTransport::set_error(ErrorCode error_code) {
    last_error_code_.store(error_code, std::memory_order_relaxed);
}

} // namespace trading::oms
