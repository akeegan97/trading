#include "trading/decode/dispatch.hpp"

#include "trading/decode/exchanges/kalshi/extractor.hpp"
#include "trading/decode/exchanges/polymarket/extractor.hpp"

namespace trading::decode {

DecodeFn resolve_decoder(ExchangeId exchange_id) {
    switch (exchange_id) {
    case ExchangeId::kKalshi:
        return exchanges::kalshi::extract;
    case ExchangeId::kPolymarket:
        return exchanges::polymarket::extract;
    }

    return exchanges::kalshi::extract;
}

} // namespace trading::decode
