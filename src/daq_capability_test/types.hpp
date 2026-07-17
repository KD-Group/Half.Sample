#pragma once

#include <map>
#include <string>

namespace daq_capability_test {

enum class Status { Pass, Fail, Skip };

enum class ExitCategory {
    Success = 0,
    ValidationFailed = 2,
    Unsupported = 3,
    InvalidArguments = 4,
    Environment = 5,
    Driver = 6,
    Output = 7
};

struct CommandResult {
    Status status = Status::Fail;
    std::string code;
    std::string message;
    std::map<std::string, std::string> evidence;
    ExitCategory exit_category = ExitCategory::ValidationFailed;
};

} // namespace daq_capability_test
