#include "daq_capability_test/adapter_factory.hpp"
#include "daq_capability_test/legacy_adapter.hpp"

namespace daq_capability_test {
std::unique_ptr<DaqAdapter> create_adapter() { return std::unique_ptr<DaqAdapter>(new LegacyDaqAdapter()); }
} // namespace daq_capability_test
