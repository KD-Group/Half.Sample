#pragma once

#include "daq_capability_test/daq_adapter.hpp"

#include <memory>

namespace daq_capability_test {
std::unique_ptr<DaqAdapter> create_adapter();
}
