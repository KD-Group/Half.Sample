#ifndef INSTANT_AI_HPP
#define INSTANT_AI_HPP

#include <vector>
#include <cmath>
#include <limits>

namespace Sampler {
namespace InstantAi {

struct TimedReading {
    double planned_seconds;
    double actual_seconds;    // ReadAny start; used for phase, ordering, and lateness.
    double voltage;
    bool read_success;
    int read_error_code;
    double completed_seconds; // ReadAny completion; used for timeout and acquisition duration.

    TimedReading(double planned = 0.0, double actual = 0.0, double value = 0.0, bool succeeded = true,
                 int error_code = 0, double completed = std::numeric_limits<double>::quiet_NaN())
        : planned_seconds(planned), actual_seconds(actual), voltage(value), read_success(succeeded),
          read_error_code(error_code), completed_seconds(std::isfinite(completed) ? completed : actual) {}
};

using TimedWaveform = std::vector<TimedReading>;
using TimedReadings = std::vector<TimedReading>;

struct Schedule {
    std::vector<double> planned_seconds;
    double deadline_seconds = 0.0;
};

struct ContinuousSchedule {
    std::vector<double> planned_seconds;
    double duration_seconds = 0.0;
    double polling_frequency_hz = 0.0;
};

struct ReadTiming {
    double actual_seconds = 0.0;
    double completed_seconds = 0.0;
    bool late = false;
    bool timed_out = false;
};

enum class ReconstructionStatus {
    Success,
    AlignmentFailed,
    WaveformCountInsufficient,
    CoverageInsufficient
};

struct ReconstructionResult {
    bool success = false;
    ReconstructionStatus status = ReconstructionStatus::AlignmentFailed;
    std::vector<double> averaged_half_wave;
    int complete_waveforms = 0;
    int interpolated_bins = 0;
    int late_reads = 0;
};

int phase_bin(double seconds, double emitting_frequency, int target_points);
Schedule build_schedule(double emitting_frequency, int target_points, double minimum_interval_seconds);
ContinuousSchedule build_continuous_schedule(double emitting_frequency, int requested_waveforms, int target_points,
                                             double max_polling_frequency_hz);
ReadTiming evaluate_read_timing(double planned_seconds, double started_seconds, double completed_seconds,
                               double previous_planned_seconds, double deadline_seconds);
ReconstructionResult reconstruct_continuous(const TimedReadings& readings, int requested_waveforms,
                                            double emitting_frequency, int target_points);
ReconstructionResult reconstruct_legacy_waveforms(const std::vector<TimedWaveform>& waveforms,
                                                  int requested_waveforms, double emitting_frequency,
                                                  int target_points);
ReconstructionResult reconstruct(const std::vector<TimedWaveform>& waveforms, int requested_waveforms,
                                 double emitting_frequency, int target_points);

} // namespace InstantAi
} // namespace Sampler

#endif
