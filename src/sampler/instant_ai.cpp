#include "instant_ai.hpp"

#include "../constant.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Sampler {
namespace InstantAi {

int phase_bin(double seconds, double emitting_frequency, int target_points) {
    double cycles = seconds * emitting_frequency;
    double phase = cycles - std::floor(cycles);
    int bin = static_cast<int>(phase * target_points);
    return std::max(0, std::min(target_points - 1, bin));
}

Schedule build_schedule(double emitting_frequency, int target_points, double minimum_interval_seconds) {
    if (emitting_frequency <= 0 || target_points <= 0 || minimum_interval_seconds <= 0) {
        throw std::invalid_argument("invalid Instant AI schedule");
    }
    Schedule result;
    std::vector<bool> covered(static_cast<std::size_t>(target_points), false);
    const double period = 1.0 / emitting_frequency;
    while (result.planned_seconds.size() < static_cast<std::size_t>(target_points)) {
        const double earliest =
            result.planned_seconds.empty() ? 0.0 : result.planned_seconds.back() + minimum_interval_seconds;
        double best = std::numeric_limits<double>::infinity();
        int best_bin = -1;
        for (int bin = 0; bin < target_points; ++bin) {
            if (covered[static_cast<std::size_t>(bin)]) {
                continue;
            }
            const double center = (bin + 0.5) * period / target_points;
            const double cycles = std::max(0.0, std::ceil((earliest - center) / period - 1e-12));
            const double candidate = center + cycles * period;
            if (candidate < best) {
                best = candidate;
                best_bin = bin;
            }
        }
        result.planned_seconds.push_back(best);
        covered[static_cast<std::size_t>(best_bin)] = true;
    }
    const double duration = result.planned_seconds.back();
    result.deadline_seconds = duration + std::max(1.0, duration * 0.1);
    return result;
}

ContinuousSchedule build_continuous_schedule(double emitting_frequency, int requested_waveforms, int target_points,
                                             double max_polling_frequency_hz) {
    if (!std::isfinite(emitting_frequency) || emitting_frequency <= 0.0 || requested_waveforms <= 0 ||
        target_points < 20 || !std::isfinite(max_polling_frequency_hz) || max_polling_frequency_hz <= 0.0) {
        throw std::invalid_argument("invalid Instant AI continuous schedule");
    }
    ContinuousSchedule result;
    result.polling_frequency_hz =
        std::min(emitting_frequency * static_cast<double>(target_points), max_polling_frequency_hz);
    result.duration_seconds = (requested_waveforms + 1.0) / emitting_frequency;
    const long long point_count =
        static_cast<long long>(std::llround(result.duration_seconds * result.polling_frequency_hz)) + 1;
    if (point_count < 2) {
        throw std::invalid_argument("invalid Instant AI continuous schedule point count");
    }
    result.planned_seconds.reserve(static_cast<std::size_t>(point_count));
    for (long long i = 0; i < point_count; ++i) {
        result.planned_seconds.push_back(i / result.polling_frequency_hz);
    }
    result.planned_seconds.back() = result.duration_seconds;
    return result;
}

namespace {
bool fill_bins(const TimedWaveform& readings, double frequency, int target_points, std::vector<double>& bins,
               int& interpolated, int& late_reads) {
    std::vector<double> sums(static_cast<std::size_t>(target_points), 0.0);
    std::vector<int> counts(static_cast<std::size_t>(target_points), 0);
    for (std::size_t i = 0; i < readings.size(); ++i) {
        const TimedReading& reading = readings[i];
        const int bin = phase_bin(reading.actual_seconds, frequency, target_points);
        sums[static_cast<std::size_t>(bin)] += reading.voltage;
        counts[static_cast<std::size_t>(bin)]++;
        const double gap = i ? reading.planned_seconds - readings[i - 1].planned_seconds : 0.1;
        if (reading.actual_seconds - reading.planned_seconds > std::max(0.020, gap * 0.5)) {
            late_reads++;
        }
    }
    bins.assign(static_cast<std::size_t>(target_points), 0.0);
    for (int i = 0; i < target_points; ++i) {
        if (counts[static_cast<std::size_t>(i)]) {
            bins[static_cast<std::size_t>(i)] = sums[static_cast<std::size_t>(i)] / counts[static_cast<std::size_t>(i)];
        }
    }
    int prefix = 0;
    while (prefix < target_points && !counts[static_cast<std::size_t>(prefix)]) {
        ++prefix;
    }
    int suffix = 0;
    while (suffix < target_points - prefix && !counts[static_cast<std::size_t>(target_points - 1 - suffix)]) {
        ++suffix;
    }
    const int boundary_missing = prefix + suffix;
    if (boundary_missing > 2 || prefix == target_points) {
        return false;
    }
    if (boundary_missing) {
        const double left = bins[static_cast<std::size_t>(target_points - 1 - suffix)];
        const double right = bins[static_cast<std::size_t>(prefix)];
        for (int i = 0; i < boundary_missing; ++i) {
            const int index = i < suffix ? target_points - suffix + i : i - suffix;
            bins[static_cast<std::size_t>(index)] =
                left + (right - left) * (i + 1) / static_cast<double>(boundary_missing + 1);
            interpolated++;
        }
    }
    for (int start = prefix; start < target_points - suffix;) {
        if (counts[static_cast<std::size_t>(start)]) {
            ++start;
            continue;
        }
        int end = start;
        while (end < target_points && !counts[static_cast<std::size_t>(end)]) {
            ++end;
        }
        const int missing = end - start;
        if (missing > 2) {
            return false;
        }
        const double left = bins[static_cast<std::size_t>(start - 1)];
        const double right = bins[static_cast<std::size_t>(end)];
        for (int j = 0; j < missing; ++j) {
            bins[static_cast<std::size_t>(start + j)] =
                left + (right - left) * (j + 1) / static_cast<double>(missing + 1);
            interpolated++;
        }
        start = end;
    }
    return true;
}

bool align_wave(std::vector<double>& wave) {
    const auto limits = std::minmax_element(wave.begin(), wave.end());
    const double span = *limits.second - *limits.first;
    if (span < Constant::MinVoltageAmplitude) {
        return false;
    }
    const double lower = *limits.first + span * Constant::LowerBound;
    const double upper = *limits.first + span * Constant::UpperBound;
    bool under = false;
    int edge = -1;
    for (int k = 0; k < static_cast<int>(wave.size()) * 2; ++k) {
        const int i = k % static_cast<int>(wave.size());
        if (wave[static_cast<std::size_t>(i)] <= lower) {
            under = true;
        } else if (under && wave[static_cast<std::size_t>(i)] >= upper) {
            edge = i;
            break;
        }
    }
    if (edge < 0) {
        return false;
    }
    std::rotate(wave.begin(), wave.begin() + edge, wave.end());
    return true;
}


bool fill_continuous_cycle(const TimedReadings& readings, double start, double end, int target_points,
                           std::vector<double>& bins, int& interpolated) {
    std::vector<double> sums(static_cast<std::size_t>(target_points), 0.0);
    std::vector<int> counts(static_cast<std::size_t>(target_points), 0);
    const double duration = end - start;
    for (const TimedReading& reading : readings) {
        if (reading.actual_seconds < start || reading.actual_seconds >= end) {
            continue;
        }
        double phase = (reading.actual_seconds - start) / duration;
        int bin = static_cast<int>(phase * target_points + 1e-7);
        bin = std::max(0, std::min(target_points - 1, bin));
        sums[static_cast<std::size_t>(bin)] += reading.voltage;
        counts[static_cast<std::size_t>(bin)]++;
    }
    bins.assign(static_cast<std::size_t>(target_points), 0.0);
    int first_present = -1;
    for (int i = 0; i < target_points; ++i) {
        if (counts[static_cast<std::size_t>(i)] > 0) {
            bins[static_cast<std::size_t>(i)] =
                sums[static_cast<std::size_t>(i)] / counts[static_cast<std::size_t>(i)];
            if (first_present < 0) {
                first_present = i;
            }
        }
    }
    if (first_present < 0) {
        return false;
    }

    int position = first_present;
    do {
        const int next = (position + 1) % target_points;
        if (counts[static_cast<std::size_t>(next)] > 0) {
            position = next;
            continue;
        }
        int missing = 0;
        int cursor = next;
        while (counts[static_cast<std::size_t>(cursor)] == 0 && cursor != position) {
            ++missing;
            cursor = (cursor + 1) % target_points;
        }
        if (missing > 2 || cursor == position) {
            return false;
        }
        const double left = bins[static_cast<std::size_t>(position)];
        const double right = bins[static_cast<std::size_t>(cursor)];
        for (int offset = 1; offset <= missing; ++offset) {
            const int index = (position + offset) % target_points;
            bins[static_cast<std::size_t>(index)] =
                left + (right - left) * offset / static_cast<double>(missing + 1);
            ++interpolated;
        }
        position = cursor;
    } while (position != first_present);
    return true;
}
} // namespace

ReconstructionResult reconstruct_continuous(const TimedReadings& input, int requested_waveforms,
                                            double emitting_frequency, int target_points) {
    ReconstructionResult result;
    if (requested_waveforms <= 0 || target_points < 20 || !std::isfinite(emitting_frequency) ||
        emitting_frequency <= 0.0) {
        return result;
    }

    TimedReadings readings;
    readings.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const TimedReading& reading = input[i];
        if (std::isfinite(reading.planned_seconds) && std::isfinite(reading.actual_seconds)) {
            const double gap = i && std::isfinite(input[i - 1].planned_seconds)
                                   ? reading.planned_seconds - input[i - 1].planned_seconds
                                   : 0.1;
            if (reading.actual_seconds - reading.planned_seconds > std::max(0.020, gap * 0.5)) {
                ++result.late_reads;
            }
        }
        if (reading.read_success && std::isfinite(reading.actual_seconds) && std::isfinite(reading.voltage)) {
            readings.push_back(reading);
        }
    }
    if (readings.empty()) {
        return result;
    }
    std::stable_sort(readings.begin(), readings.end(), [](const TimedReading& left, const TimedReading& right) {
        return left.actual_seconds < right.actual_seconds;
    });

    double minimum = readings.front().voltage;
    double maximum = readings.front().voltage;
    for (std::size_t i = 0; i < readings.size(); ++i) {
        minimum = std::min(minimum, readings[i].voltage);
        maximum = std::max(maximum, readings[i].voltage);
    }
    const double span = maximum - minimum;
    if (span < Constant::MinVoltageAmplitude) {
        return result;
    }

    const double lower = minimum + span * Constant::LowerBound;
    const double upper = minimum + span * Constant::UpperBound;
    std::vector<double> edges;
    bool armed = false;
    for (const TimedReading& reading : readings) {
        if (reading.voltage <= lower) {
            armed = true;
        } else if (armed && reading.voltage >= upper) {
            edges.push_back(reading.actual_seconds);
            armed = false;
        }
    }
    if (edges.empty()) {
        return result;
    }

    const double theoretical_period = 1.0 / emitting_frequency;
    int chain_start = -1;
    for (int start = 0; start + requested_waveforms < static_cast<int>(edges.size()); ++start) {
        bool compatible = true;
        for (int cycle = 0; cycle < requested_waveforms; ++cycle) {
            const double period = edges[static_cast<std::size_t>(start + cycle + 1)] -
                                  edges[static_cast<std::size_t>(start + cycle)];
            if (period < theoretical_period * 0.8 || period > theoretical_period * 1.2) {
                compatible = false;
                break;
            }
        }
        if (compatible) {
            chain_start = start;
            break;
        }
    }
    if (chain_start < 0) {
        result.status = ReconstructionStatus::WaveformCountInsufficient;
        return result;
    }

    result.averaged_half_wave.assign(static_cast<std::size_t>(target_points / 2), 0.0);
    for (int cycle = 0; cycle < requested_waveforms; ++cycle) {
        std::vector<double> bins;
        if (!fill_continuous_cycle(readings, edges[static_cast<std::size_t>(chain_start + cycle)],
                                   edges[static_cast<std::size_t>(chain_start + cycle + 1)], target_points, bins,
                                   result.interpolated_bins)) {
            result.status = ReconstructionStatus::CoverageInsufficient;
            result.averaged_half_wave.clear();
            return result;
        }
        const int baseline_points = std::min(10, std::max(5, target_points / 10));
        double baseline = 0.0;
        for (int i = 0; i < baseline_points; ++i) {
            baseline += bins[static_cast<std::size_t>(target_points - 1 - i)];
        }
        baseline /= baseline_points;
        for (int i = 0; i < target_points / 2; ++i) {
            result.averaged_half_wave[static_cast<std::size_t>(i)] +=
                (bins[static_cast<std::size_t>(i)] - baseline) / requested_waveforms;
        }
    }
    result.status = ReconstructionStatus::Success;
    result.success = true;
    result.complete_waveforms = requested_waveforms;
    return result;
}

ReconstructionResult reconstruct_legacy_waveforms(const std::vector<TimedWaveform>& waveforms,
                                                  int requested_waveforms, double emitting_frequency,
                                                  int target_points) {
    ReconstructionResult result;
    if (requested_waveforms <= 0 || target_points < 20 ||
        waveforms.size() != static_cast<std::size_t>(requested_waveforms)) {
        return result;
    }
    result.averaged_half_wave.assign(static_cast<std::size_t>(target_points / 2), 0.0);
    for (const auto& readings : waveforms) {
        std::vector<double> wave;
        if (!fill_bins(readings, emitting_frequency, target_points, wave, result.interpolated_bins,
                       result.late_reads) ||
            !align_wave(wave)) {
            result.averaged_half_wave.clear();
            return result;
        }
        const int baseline_points = std::min(10, std::max(5, target_points / 10));
        double baseline = 0.0;
        for (int i = 0; i < baseline_points; ++i) {
            baseline += wave[wave.size() - 1 - static_cast<std::size_t>(i)];
        }
        baseline /= baseline_points;
        for (int i = 0; i < target_points / 2; ++i) {
            result.averaged_half_wave[static_cast<std::size_t>(i)] +=
                (wave[static_cast<std::size_t>(i)] - baseline) / requested_waveforms;
        }
    }
    result.success = true;
    result.status = ReconstructionStatus::Success;
    result.complete_waveforms = requested_waveforms;
    return result;
}

ReconstructionResult reconstruct(const std::vector<TimedWaveform>& waveforms, int requested_waveforms,
                                 double emitting_frequency, int target_points) {
    return reconstruct_legacy_waveforms(waveforms, requested_waveforms, emitting_frequency, target_points);
}

} // namespace InstantAi
} // namespace Sampler
