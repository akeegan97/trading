#include "trading/decode/exchanges/polymarket/extractor.hpp"

namespace trading::decode::exchanges::polymarket {

ExtractedFields extract(const ingest::RawFrame& frame) {
    ExtractedFields fields;
    fields.status = DecodeStatus::kMissingField;
    fields.recv_timestamp = frame.recv_timestamp;
    fields.source = frame.source;
    return fields;
}

} // namespace trading::decode::exchanges::polymarket
