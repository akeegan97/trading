#include "trading/decode/exchanges/kalshi/extractor.hpp"

namespace trading::decode::exchanges::kalshi {

ExtractedFields extract(const ingest::RawFrame& frame) {
    ExtractedFields fields;
    fields.status = DecodeStatus::kMissingField;
    fields.recv_timestamp = frame.recv_timestamp;
    fields.source = frame.source;
    return fields;
}

} // namespace trading::decode::exchanges::kalshi
