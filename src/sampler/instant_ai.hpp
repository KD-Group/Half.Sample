#ifndef INSTANT_AI_HPP
#define INSTANT_AI_HPP

#include <vector>

namespace Sampler {
namespace InstantAi {

struct TimedReading {
    double planned_seconds;
    double actual_seconds;
    double voltage;
};

using TimedWaveform = std::vector<TimedReading>;

struct Schedule {
    std::vector<double> planned_seconds;
    double deadline_seconds = 0.0;
};

struct ReconstructionResult {
    bool success = false;
    std::vector<double> averaged_half_wave;
    int interpolated_bins = 0;
    int late_reads = 0;
};

int phase_bin(double seconds, double emitting_frequency, int target_points);
Schedule build_schedule(double emitting_frequency, int target_points, double minimum_interval_seconds);
ReconstructionResult reconstruct(const std::vector<TimedWaveform>& waveforms, int requested_waveforms,
                                 double emitting_frequency, int target_points);

} // namespace InstantAi
} // namespace Sampler

#endif
