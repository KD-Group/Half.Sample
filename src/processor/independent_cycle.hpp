#ifndef INDEPENDENT_CYCLE_HPP
#define INDEPENDENT_CYCLE_HPP

#include <cstddef>
#include <vector>

namespace Commander {
namespace Processor {

struct IndependentCycleResult {
    bool success = false;
    int candidate_waveforms = 0;
    int accepted_waveforms = 0;
    int discarded_waveforms = 0;
    int rejected_threshold_candidates = 0;
    int rejected_short_periods = 0;
    int rejected_long_periods = 0;
    int rejected_amplitude_cycles = 0;
    int rejected_baseline_cycles = 0;
    int rejected_shape_cycles = 0;
    std::vector<std::vector<double>> cycles;
};

IndependentCycleResult extract_independent_cycles(const std::vector<double>& samples,
                                                  int sampling_frequency,
                                                  double emitting_frequency,
                                                  int requested_waveforms);

} // namespace Processor
} // namespace Commander

#endif
