#include <algorithm>
#include <cmath>
#include <ctime>
#include <iostream>
#include <limits>
#include "../constant.hpp"
#include "mock_sampler.hpp"

namespace Sampler {

MockSampler::MockSampler() {
    mock_tau = Constant::DefaultMockTau;
    mock_v0 = Constant::DefaultMockV0;
    mock_v_inf = Constant::DefaultMockVInf;
    mock_noise = Constant::DefaultMockNoise;
    mock_is_going_down = 0.0;
    mock_phase_offset = 0.0;
    mock_missing_bin_start = 0;
    mock_missing_bin_count = 0;
    mock_work_iterations = 0;
}

bool MockSampler::sample(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.is_instant()) {
        volatile double work = 0.0;
        for (int i = 0; i < mock_work_iterations; ++i) {
            work += static_cast<double>(i & 1);
            if ((i & 1023) == 0) {
                if (result.progress.cancel_requested.load(std::memory_order_acquire)) {
                    result.error_code = Error::USER_CANCELLED;
                    result.cancelled = true;
                    result.instant_ai_actual_duration_seconds =
                        result.progress.elapsed_milliseconds.load() / 1000.0;
                    dump_origin_data(config, result);
                    return false;
                }
                const double fraction = mock_work_iterations > 0
                                            ? static_cast<double>(i) / mock_work_iterations
                                            : 0.0;
                result.progress.update_elapsed(config.instant_ai_planned_duration_seconds * fraction);
            }
        }
        (void)work;
        result.instant_ai_waveforms.clear();
        result.instant_ai_readings.clear();
        result.instant_ai_format_version = 2;
        std::vector<double>().swap(result.totalSamplingBuffer);
        const auto schedule = InstantAi::build_continuous_schedule(
            config.emitting_frequency, config.number_of_waveforms, config.instant_ai_target_points_per_waveform,
            config.instant_ai_max_reliable_polling_hz);
        for (double planned : schedule.planned_seconds) {
            if (result.progress.cancel_requested.load(std::memory_order_acquire)) {
                result.error_code = Error::USER_CANCELLED;
                result.cancelled = true;
                result.instant_ai_actual_duration_seconds =
                    result.progress.elapsed_milliseconds.load() / 1000.0;
                dump_origin_data(config, result);
                return false;
            }
            const double cycles = planned * config.emitting_frequency + mock_phase_offset;
            const double phase = cycles - std::floor(cycles);
            const int phase_bin = static_cast<int>(phase * config.instant_ai_target_points_per_waveform + 1e-7);
            const int points = config.instant_ai_target_points_per_waveform;
            const int normalized_missing_start = ((mock_missing_bin_start % points) + points) % points;
            const int missing_count = std::min(mock_missing_bin_count, points);
            const int missing_offset = ((phase_bin - normalized_missing_start) % points + points) % points;
            const bool terminal_read = planned == schedule.planned_seconds.back();
            if (!terminal_read && missing_count > 0 && missing_offset < missing_count) {
                continue;
            }
            double voltage = 0.0;
            if (phase < 0.5) {
                const double time_us = phase / config.emitting_frequency * 1e6;
                voltage = mock_v_inf + (mock_v0 - mock_v_inf) * std::exp(time_us / -mock_tau);
                if (mock_is_going_down) {
                    voltage += (mock_v0 - mock_v_inf) / 3 *
                               std::exp((0.5 / config.emitting_frequency * 1e6 - time_us) / -mock_tau);
                }
            }
            result.instant_ai_readings.push_back({planned, planned, voltage});
            result.progress.successful_reads.fetch_add(1);
            result.progress.update_elapsed(planned);
            result.progress.completed_cycles.store(std::min(
                result.progress.target_cycles.load(), static_cast<int>(std::floor(planned * config.emitting_frequency))));
        }
        result.instant_ai_actual_duration_seconds = schedule.duration_seconds;
        result.progress.update_elapsed(schedule.duration_seconds);
        return dump_origin_data(config, result);
    }

    auto buffer = result.totalSamplingBuffer.data();
    const int transition_length = 5;
    for (std::size_t i = 0; i < result.totalSamplingBuffer.size(); ++i) {
        const int point = static_cast<int>(i % static_cast<std::size_t>(config.waveform_length));
        if (point < transition_length) {
            buffer[i] = point * mock_v0 / transition_length;
        } else if (point < config.waveform_length / 2) {
                double b = mock_v_inf;
                double w = mock_v0 - b;
                buffer[i] = b + w * exp((point - transition_length) * config.sampling_interval / -mock_tau);

                if (mock_is_going_down) {
                    buffer[i] += w / 3 *
                                 exp((config.waveform_length / 2 - point) * config.sampling_interval / -mock_tau);
                }
        } else if (point < config.waveform_length / 2 + transition_length) {
            buffer[i] = (transition_length - (point - config.waveform_length / 2)) *
                        mock_v_inf / transition_length;
        } else {
            buffer[i] = 0;
        }
    }

    // random noise
    srand(time(nullptr));
    for (std::size_t i = 0; i < result.totalSamplingBuffer.size(); ++i) {
        buffer[i] += (rand() / double(RAND_MAX) - 0.5) * mock_noise;
    }

    // vertical shift
    const double max_shift_amplitude = -2.5;
    const double shift_amplitude = rand() / double(RAND_MAX) * max_shift_amplitude;
    for (std::size_t i = 0; i < result.totalSamplingBuffer.size(); ++i) {
        buffer[i] += shift_amplitude;
    }

    dump_origin_data(config, result);

    return true;
}

double MockSampler::get_value(const std::string& key) {
    double value;
    if (key == "mock_tau") {
        value = mock_tau;
    } else if (key == "mock_v0") {
        value = mock_v0;
    } else if (key == "mock_v_inf") {
        value = mock_v_inf;
    } else if (key == "mock_noise") {
        value = mock_noise;
    } else if (key == "mock_is_going_down") {
        value = mock_is_going_down;
    } else if (key == "mock_phase_offset") {
        value = mock_phase_offset;
    } else if (key == "mock_missing_bin_start") {
        value = mock_missing_bin_start;
    } else if (key == "mock_missing_bin_count") {
        value = mock_missing_bin_count;
    } else if (key == "mock_work_iterations") {
        value = mock_work_iterations;
    } else {
        return 0.0;
    }

    return value;
}

bool MockSampler::set_value(const std::string& key, const double value) {
    if (key == "mock_tau") {
        mock_tau = value;
    } else if (key == "mock_v0") {
        mock_v0 = value;
    } else if (key == "mock_v_inf") {
        mock_v_inf = value;
    } else if (key == "mock_noise") {
        mock_noise = value;
    } else if (key == "mock_is_going_down") {
        mock_is_going_down = value;
    } else if (key == "mock_phase_offset") {
        if (!std::isfinite(value)) {
            return false;
        }
        mock_phase_offset = value - std::floor(value);
    } else if (key == "mock_missing_bin_start") {
        if (!std::isfinite(value) || std::floor(value) != value ||
            value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
            return false;
        }
        mock_missing_bin_start = static_cast<int>(value);
    } else if (key == "mock_missing_bin_count") {
        if (!std::isfinite(value) || std::floor(value) != value || value < 0 ||
            value > std::numeric_limits<int>::max()) {
            return false;
        }
        mock_missing_bin_count = static_cast<int>(value);
    } else if (key == "mock_work_iterations") {
        if (!std::isfinite(value) || std::floor(value) != value || value < 0 ||
            value > std::numeric_limits<int>::max()) {
            return false;
        }
        mock_work_iterations = static_cast<int>(value);
    } else {
        return false;
    }

    return true;
}

std::string MockSampler::name() { return "mock_sampler"; }

} // namespace Sampler
