#ifndef SAMPLING_RESULT_HPP
#define SAMPLING_RESULT_HPP

#include "../constant.hpp"
#include "../error/error.hpp"
#include "../estimate/estimate.hpp"
#include "../sampler/instant_ai.hpp"
#include <vector>

namespace Result {

struct SamplingResult {
    std::vector<double> totalSamplingBuffer = std::vector<double>(Constant::MaxBufferSize, 0.0);
    std::vector<double> resultWave = std::vector<double>(Constant::MaxBufferSize / 16, 0.0);
    std::vector<Sampler::InstantAi::TimedWaveform> instant_ai_waveforms;
    int instant_ai_late_reads{};
    int instant_ai_interpolated_bins{};

    double maximum{}, minimum{};
    Estimate::EstimatedResult estimate;

    bool success{};
    bool measuring{};
    Error::Code error_code = Error::Code::SUCCESS;
};

} // namespace Result

#endif
