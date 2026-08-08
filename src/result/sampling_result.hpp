#ifndef SAMPLING_RESULT_HPP
#define SAMPLING_RESULT_HPP

#include "../constant.hpp"
#include "../error/error.hpp"
#include "../estimate/estimate.hpp"
#include "../sampler/instant_ai.hpp"
#include <atomic>
#include <vector>

namespace Result {

struct SamplingProgress {
    std::atomic<long long> planned_milliseconds{0};
    std::atomic<long long> elapsed_milliseconds{0};
    std::atomic<int> completed_cycles{0};
    std::atomic<int> target_cycles{0};
    std::atomic<int> successful_reads{0};
    std::atomic<int> late_reads{0};
    std::atomic<bool> cancel_requested{false};

    void reset(double planned_seconds, int cycles) {
        planned_milliseconds.store(static_cast<long long>(planned_seconds * 1000.0));
        elapsed_milliseconds.store(0);
        completed_cycles.store(0);
        target_cycles.store(cycles);
        successful_reads.store(0);
        late_reads.store(0);
        cancel_requested.store(false);
    }
};

struct SamplingResult {
    SamplingProgress progress;
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
