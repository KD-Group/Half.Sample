#include "independent_cycle.hpp"

#include "../constant.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Commander {
namespace Processor {
namespace {

double percentile(const std::vector<double>& values, double fraction) {
    std::vector<double> sorted(values);
    std::sort(sorted.begin(), sorted.end());
    const double position = fraction * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(position);
    const auto upper = std::min(lower + 1, sorted.size() - 1);
    const double ratio = position - static_cast<double>(lower);
    return sorted[lower] * (1.0 - ratio) + sorted[upper] * ratio;
}

double cycle_percentile(const std::vector<double>& cycle, double fraction) {
    return percentile(cycle, fraction);
}

double median_window(const std::vector<double>& samples, std::size_t begin, std::size_t end) {
    std::vector<double> window(samples.begin() + static_cast<std::ptrdiff_t>(begin),
                               samples.begin() + static_cast<std::ptrdiff_t>(end));
    return percentile(window, 0.5);
}

double mean_window(const std::vector<double>& samples, std::size_t begin, std::size_t end) {
    const double sum = std::accumulate(samples.begin() + static_cast<std::ptrdiff_t>(begin),
                                       samples.begin() + static_cast<std::ptrdiff_t>(end), 0.0);
    return sum / static_cast<double>(end - begin);
}

double correlation(const std::vector<double>& left, const std::vector<double>& right) {
    if (left.size() != right.size() || left.empty()) return 0.0;
    const double left_mean = std::accumulate(left.begin(), left.end(), 0.0) / left.size();
    const double right_mean = std::accumulate(right.begin(), right.end(), 0.0) / right.size();
    double numerator = 0.0;
    double left_sum = 0.0;
    double right_sum = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const double left_delta = left[i] - left_mean;
        const double right_delta = right[i] - right_mean;
        numerator += left_delta * right_delta;
        left_sum += left_delta * left_delta;
        right_sum += right_delta * right_delta;
    }
    if (left_sum <= 0.0 || right_sum <= 0.0) return 0.0;
    return numerator / std::sqrt(left_sum * right_sum);
}

std::vector<std::size_t> find_confirmed_edges(const std::vector<double>& samples,
                                              double lower_bound, double upper_bound,
                                              int confirmation_points) {
    const std::size_t window = static_cast<std::size_t>(confirmation_points);
    if (samples.size() < window) return {};

    std::vector<unsigned char> below(samples.size());
    std::vector<unsigned char> above(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        below[i] = samples[i] <= lower_bound;
        above[i] = samples[i] >= upper_bound;
    }

    int below_count = 0;
    int above_count = 0;
    for (std::size_t i = 0; i < window; ++i) {
        below_count += below[i];
        above_count += above[i];
    }

    std::vector<std::size_t> edges;
    bool waiting_for_high = false;
    for (std::size_t i = 0; i + window <= samples.size(); ++i) {
        const bool confirmed_low = below_count * 2 >= confirmation_points;
        const bool confirmed_high = above_count * 2 >= confirmation_points;
        if (!waiting_for_high) {
            if (confirmed_low) waiting_for_high = true;
        } else if (confirmed_high) {
            edges.push_back(i);
            waiting_for_high = false;
        }

        if (i + window < samples.size()) {
            below_count += static_cast<int>(below[i + window]) - static_cast<int>(below[i]);
            above_count += static_cast<int>(above[i + window]) - static_cast<int>(above[i]);
        }
    }
    return edges;
}

} // namespace

IndependentCycleResult extract_independent_cycles(const std::vector<double>& samples,
                                                  int sampling_frequency,
                                                  double emitting_frequency,
                                                  int requested_waveforms) {
    IndependentCycleResult result;
    if (sampling_frequency <= 0 || !std::isfinite(emitting_frequency) || emitting_frequency <= 0.0 ||
        requested_waveforms <= 0 || samples.empty()) {
        return result;
    }

    const int waveform_length = static_cast<int>(std::llround(sampling_frequency / emitting_frequency));
    if (waveform_length <= 0) return result;

    const auto raw_limits = std::minmax_element(samples.begin(), samples.end());
    if (*raw_limits.second - *raw_limits.first < Constant::MinVoltageAmplitude) return result;
    const double minimum = *raw_limits.first;
    const double maximum = *raw_limits.second;
    const double span = maximum - minimum;
    if (span <= 0.0) return result;

    const double lower_bound = minimum + span * Constant::LowerBound;
    const double upper_bound = minimum + span * Constant::UpperBound;
    const int confirmation_points = std::max(32, static_cast<int>(std::llround(waveform_length * 0.00005)));
    const auto phase_starts = find_confirmed_edges(samples, lower_bound, upper_bound, confirmation_points);
    result.candidate_waveforms = static_cast<int>(phase_starts.size());
    if (phase_starts.empty()) return result;

    std::size_t anchor = phase_starts.front();
    std::vector<double> amplitudes;
    std::vector<double> baselines;
    std::vector<double> template_cycle;
    result.cycles.reserve(static_cast<std::size_t>(requested_waveforms));

    for (std::size_t edge_index = 1; edge_index < phase_starts.size(); ++edge_index) {
        const std::size_t end = phase_starts[edge_index];
        const std::size_t source_length = end - anchor;
        if (source_length < static_cast<std::size_t>(waveform_length * 0.90)) {
            ++result.rejected_short_periods;
            continue;
        }
        if (source_length > static_cast<std::size_t>(waveform_length * 1.10)) {
            ++result.rejected_long_periods;
            anchor = end;
            continue;
        }
        if (anchor < static_cast<std::size_t>(Constant::CroppedLength)) {
            anchor = end;
            continue;
        }

        double baseline = mean_window(samples, anchor - Constant::CroppedLength,
                                       anchor - Constant::CroppedLength / 2);
        std::vector<double> cycle(static_cast<std::size_t>(waveform_length));
        for (int point = 0; point < waveform_length; ++point) {
            const double source_position = static_cast<double>(point) * static_cast<double>(source_length - 1) /
                                           static_cast<double>(waveform_length - 1);
            const std::size_t left = static_cast<std::size_t>(source_position);
            const std::size_t right = std::min(left + 1, source_length - 1);
            const double fraction = source_position - static_cast<double>(left);
            cycle[static_cast<std::size_t>(point)] =
                samples[anchor + left] * (1.0 - fraction) + samples[anchor + right] * fraction - baseline;
        }

        const double amplitude = cycle_percentile(cycle, 0.99) - cycle_percentile(cycle, 0.01);
        if (amplitudes.size() >= 3) {
            const double amplitude_reference = percentile(amplitudes, 0.5);
            if (amplitude < amplitude_reference * 0.70 || amplitude > amplitude_reference * 1.30) {
                ++result.rejected_amplitude_cycles;
                anchor = end;
                continue;
            }
            const double baseline_reference = percentile(baselines, 0.5);
            if (std::fabs(baseline - baseline_reference) > std::max(0.005, amplitude_reference * 0.20)) {
                ++result.rejected_baseline_cycles;
                anchor = end;
                continue;
            }
            if (!template_cycle.empty()) {
                double squared_error = 0.0;
                for (std::size_t i = 0; i < cycle.size(); ++i) {
                    const double difference = cycle[i] - template_cycle[i];
                    squared_error += difference * difference;
                }
                const double shape_error = std::sqrt(squared_error / cycle.size()) / amplitude_reference;
                if (shape_error > 0.15 || correlation(cycle, template_cycle) < 0.90) {
                    ++result.rejected_shape_cycles;
                    anchor = end;
                    continue;
                }
            }
        }

        result.cycles.emplace_back(std::move(cycle));
        amplitudes.push_back(amplitude);
        baselines.push_back(baseline);
        if (template_cycle.empty()) {
            template_cycle = result.cycles.back();
        } else {
            for (std::size_t i = 0; i < template_cycle.size(); ++i)
                template_cycle[i] = (template_cycle[i] * static_cast<double>(result.cycles.size() - 1) +
                                     result.cycles.back()[i]) /
                                    static_cast<double>(result.cycles.size());
        }
        anchor = end;
        if (static_cast<int>(result.cycles.size()) == requested_waveforms) break;
    }

    result.accepted_waveforms = static_cast<int>(result.cycles.size());
    result.discarded_waveforms = std::max(0, result.candidate_waveforms - result.accepted_waveforms);
    result.success = result.accepted_waveforms == requested_waveforms;
    return result;
}

} // namespace Processor
} // namespace Commander
