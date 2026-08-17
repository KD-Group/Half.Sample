#include "independent_cycle.hpp"

#include <cmath>

namespace Commander {
namespace Processor {

IndependentCycleResult extract_independent_cycles(const std::vector<double>& samples,
                                                  int sampling_frequency,
                                                  double emitting_frequency,
                                                  int requested_waveforms) {
    IndependentCycleResult result;
    if (sampling_frequency <= 0 || !std::isfinite(emitting_frequency) || emitting_frequency <= 0.0 ||
        requested_waveforms <= 0) {
        return result;
    }

    const int waveform_length = static_cast<int>(std::llround(sampling_frequency / emitting_frequency));
    if (waveform_length <= 0) {
        return result;
    }

    result.candidate_waveforms = static_cast<int>(samples.size() / static_cast<std::size_t>(waveform_length));
    if (result.candidate_waveforms < requested_waveforms + 1) {
        return result;
    }

    // The first complete period is a guard period. No raw samples are joined
    // across independent acquisitions; only complete period vectors are kept.
    result.discarded_waveforms = 1;
    result.cycles.reserve(static_cast<std::size_t>(requested_waveforms));
    for (int cycle = 0; cycle < requested_waveforms; ++cycle) {
        const std::size_t start = static_cast<std::size_t>(cycle + 1) * static_cast<std::size_t>(waveform_length);
        const std::size_t end = start + static_cast<std::size_t>(waveform_length / 2);
        if (end > samples.size()) {
            result.cycles.clear();
            result.accepted_waveforms = 0;
            result.success = false;
            return result;
        }
        result.cycles.emplace_back(samples.begin() + static_cast<std::ptrdiff_t>(start),
                                   samples.begin() + static_cast<std::ptrdiff_t>(end));
    }

    result.accepted_waveforms = static_cast<int>(result.cycles.size());
    result.success = result.accepted_waveforms == requested_waveforms;
    return result;
}

} // namespace Processor
} // namespace Commander
