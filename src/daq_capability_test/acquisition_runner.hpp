#pragma once

#include "daq_capability_test/daq_adapter.hpp"
#include "daq_capability_test/matrix.hpp"
#include <functional>

namespace daq_capability_test {
typedef std::function<CommandResult(unsigned int, const AcquisitionData&)> AcquisitionObserver;
CommandResult run_acquisition_case(DaqAdapter& adapter, const MatrixCase& matrix_case,
                                   const AcquisitionObserver& observer = AcquisitionObserver());
CommandResult validate_acquisition_data(const AcquisitionData& data, const AcquisitionRequest& request,
                                        const MatrixCase* matrix_case = 0);
} // namespace daq_capability_test
