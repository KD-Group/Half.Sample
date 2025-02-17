#ifndef SAMPLING_RESULT_HPP
#define SAMPLING_RESULT_HPP

#include "../constant.hpp"
#include "../error/error.hpp"
#include "../estimate/estimate.hpp"
#include <vector>

namespace Result {

    struct SamplingResult {
        std::vector<double> totalSamplingBuffer = std::vector<double>(Constant::MaxBufferSize, 0.0);
        std::vector<double> resultWave = std::vector<double>(Constant::MaxBufferSize / 16, 0.0);

        double maximum{}, minimum{};
        Estimate::EstimatedResult estimate;

        bool success{};
        bool measuring{};
        Error::Code error_code = Error::Code::SUCCESS;
    };

} // namespace Result;

#endif
