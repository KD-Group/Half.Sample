#include "independent_cycle.hpp"

#include "../constant.hpp"

#include <algorithm>
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

    const auto limits = std::minmax_element(samples.begin(), samples.end());
    const double minimum = *limits.first;
    const double maximum = *limits.second;
    const double span = maximum - minimum;
    if (span < Constant::MinVoltageAmplitude) {
        return result;
    }

    const double lower_bound = minimum + span * Constant::LowerBound;
    const double upper_bound = minimum + span * Constant::UpperBound;
    bool under_lower_bound = false;
    std::vector<std::size_t> phase_starts;
    phase_starts.reserve(samples.size() / static_cast<std::size_t>(waveform_length));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (samples[i] <= lower_bound) {
            under_lower_bound = true;
        } else if (under_lower_bound && samples[i] >= upper_bound) {
            const bool separated_from_previous =
                phase_starts.empty() ||
                i - phase_starts.back() >= static_cast<std::size_t>(waveform_length * 0.5);
            if (separated_from_previous) {
                phase_starts.push_back(i);
            }
            under_lower_bound = false;
        }
    }
    if (phase_starts.empty()) {
        return result;
    }

    result.candidate_waveforms = static_cast<int>(phase_starts.size());
    if (result.candidate_waveforms < requested_waveforms + 1) {
        return result;
    }

    // N complete periods require N+1 phase edges. The final edge is the guard
    // boundary; any additional detected periods remain unused. Resampling each
    // edge-to-edge interval aligns cycles despite threshold-crossing jitter.
    result.discarded_waveforms = result.candidate_waveforms - requested_waveforms;
    result.cycles.reserve(static_cast<std::size_t>(requested_waveforms));
    for (int cycle = 0; cycle < requested_waveforms; ++cycle) {
        const std::size_t start = phase_starts[static_cast<std::size_t>(cycle)];
        const std::size_t end = phase_starts[static_cast<std::size_t>(cycle + 1)];
        const std::size_t source_length = end - start;
        if (start < static_cast<std::size_t>(Constant::CroppedLength) || source_length == 0 ||
            source_length < static_cast<std::size_t>(waveform_length / 2) ||
            source_length > static_cast<std::size_t>(waveform_length * 3 / 2)) {
            result.cycles.clear();
            result.accepted_waveforms = 0;
            result.success = false;
            return result;
        }

        double base_sum = 0.0;
        for (int j = 0; j < Constant::CroppedLength / 2; ++j) {
            base_sum += samples[start - Constant::CroppedLength + static_cast<std::size_t>(j)];
        }
        const double base_value = base_sum / (Constant::CroppedLength / 2);

        std::vector<double> aligned_cycle(static_cast<std::size_t>(waveform_length));
        for (int point = 0; point < waveform_length; ++point) {
            const double source_position =
                static_cast<double>(point) * static_cast<double>(source_length - 1) /
                static_cast<double>(waveform_length - 1);
            const std::size_t left = static_cast<std::size_t>(source_position);
            const std::size_t right = std::min(left + 1, source_length - 1);
            const double fraction = source_position - static_cast<double>(left);
            const double value = samples[start + left] * (1.0 - fraction) + samples[start + right] * fraction;
            aligned_cycle[static_cast<std::size_t>(point)] = value - base_value;
        }
        result.cycles.emplace_back(std::move(aligned_cycle));
    }

    result.accepted_waveforms = static_cast<int>(result.cycles.size());
    result.success = result.accepted_waveforms == requested_waveforms;
    return result;
}

} // namespace Processor
} // namespace Commander
