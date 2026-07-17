#include "daq_capability_test/cli.hpp"
#ifdef DAQ_CAPABILITY_STANDALONE
#include "daq_capability_test/adapter_factory.hpp"
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif
#endif

namespace daq_capability_test {
int daq_capability_main(DaqAdapter& adapter, int argc, char** argv) { return run_cli(adapter, argc, argv); }
} // namespace daq_capability_test

#ifdef DAQ_CAPABILITY_STANDALONE
int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::unique_ptr<daq_capability_test::DaqAdapter> adapter = daq_capability_test::create_adapter();
    return daq_capability_test::daq_capability_main(*adapter, argc, argv);
}
#endif
