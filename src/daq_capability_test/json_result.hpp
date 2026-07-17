#pragma once

#include "daq_capability_test/types.hpp"

#include <string>

namespace daq_capability_test {

std::string to_json(const CommandResult& result);
std::string json_string(const std::string& value);

} // namespace daq_capability_test
