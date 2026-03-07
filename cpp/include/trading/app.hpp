#pragma once

#include <string>
#include <string_view>

namespace trading {

std::string build_heartbeat_json(std::string_view mode);

} // namespace trading
