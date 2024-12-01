#ifndef SAMPLING_RESULT_HPP
#define SAMPLING_RESULT_HPP

#include "../constant.hpp"
#include "../error/error.hpp"
#include "../estimate/estimate.hpp"

namespace Result {

    struct SamplingResult {
        double buffer[Constant::MaxBufferSize]{};
        double wave[Constant::MaxBufferSize / 16]{};

        double maximum{}, minimum{};
        Estimate::EstimatedResult estimate;

        bool success{};
        bool measuring{};
        Error::Code error_code = Error::Code::SUCCESS;
    };

} // namespace Result;

#endif
