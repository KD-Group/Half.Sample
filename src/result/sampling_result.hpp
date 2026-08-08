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
    Sampler::InstantAi::TimedReadings instant_ai_readings;
    int instant_ai_format_version{};
    int instant_ai_complete_waveforms{};
    double instant_ai_actual_duration_seconds{};
    int instant_ai_late_reads{};
    int instant_ai_interpolated_bins{};
    bool cancelled{};

    double maximum{}, minimum{};
    Estimate::EstimatedResult estimate;

    bool success{};
    bool measuring{};
    Error::Code error_code = Error::Code::SUCCESS;
};

} // namespace Result

#endif
