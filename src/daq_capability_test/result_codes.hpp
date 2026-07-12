#pragma once

#include "daq_capability_test/types.hpp"

namespace daq_capability_test {

inline int exit_code(const CommandResult& result)
{
    switch (result.status) {
    case Status::Pass:
        return result.exit_category == ExitCategory::Success ? 0 : 4;
    case Status::Skip:
        return result.exit_category == ExitCategory::Unsupported ? 3 : 4;
    case Status::Fail:
        switch (result.exit_category) {
        case ExitCategory::ValidationFailed: return 2;
        case ExitCategory::InvalidArguments: return 4;
        case ExitCategory::Environment: return 5;
        case ExitCategory::Driver: return 6;
        case ExitCategory::Output: return 7;
        case ExitCategory::Success:
        case ExitCategory::Unsupported:
            return 4;
        }
    }
    return 4;
}

}  // namespace daq_capability_test
