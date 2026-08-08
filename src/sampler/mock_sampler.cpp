#include <cmath>
#include <ctime>
#include <iostream>
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
}

bool MockSampler::sample(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.is_instant()) {
        result.instant_ai_waveforms.clear();
        result.instant_ai_readings.clear();
        result.instant_ai_format_version = 2;
        const auto schedule = InstantAi::build_continuous_schedule(
            config.emitting_frequency, config.number_of_waveforms, config.instant_ai_target_points_per_waveform,
            config.instant_ai_max_reliable_polling_hz);
        for (double planned : schedule.planned_seconds) {
            const double cycles = planned * config.emitting_frequency + mock_phase_offset;
            const double phase = cycles - std::floor(cycles);
            const int phase_bin = static_cast<int>(phase * config.instant_ai_target_points_per_waveform + 1e-7);
            const int missing_offset =
                (phase_bin - mock_missing_bin_start + config.instant_ai_target_points_per_waveform) %
                config.instant_ai_target_points_per_waveform;
            if (mock_missing_bin_count > 0 && missing_offset < mock_missing_bin_count) {
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
        }
        result.instant_ai_actual_duration_seconds = schedule.duration_seconds;
        result.totalSamplingBuffer.clear();
        result.totalSamplingBuffer.reserve(result.instant_ai_readings.size());
        for (const auto& reading : result.instant_ai_readings) {
            result.totalSamplingBuffer.push_back(reading.voltage);
        }
        dump_origin_data(config, result);
        return true;
    }

    auto buffer = result.totalSamplingBuffer.data();

    for (int i = 0; i < config.number_of_waveforms + 1; i++) {
        auto current = buffer + config.waveform_length * i;

        const int transition_length = 5;
        for (int j = 0; j < config.waveform_length; j++) {
            if (j < transition_length) {
                current[j] = j * mock_v0 / transition_length;
            } else if (j < config.waveform_length / 2) {
                double b = mock_v_inf;
                double w = mock_v0 - b;
                current[j] = b + w * exp((j - transition_length) * config.sampling_interval / -mock_tau);

                if (mock_is_going_down) {
                    current[j] += w / 3 * exp((config.waveform_length / 2 - j) * config.sampling_interval / -mock_tau);
                }
            } else if (j < config.waveform_length / 2 + transition_length) {
                current[j] = (transition_length - (j - config.waveform_length / 2)) * mock_v_inf / transition_length;
            } else {
                current[j] = 0;
            }
        }
    }

    // random noise
    srand(time(nullptr));
    for (int i = 0; i < config.sampling_length_per_sample; i++) {
        buffer[i] += (rand() / double(RAND_MAX) - 0.5) * mock_noise;
    }

    // vertical shift
    const double max_shift_amplitude = -2.5;
    const double shift_amplitude = rand() / double(RAND_MAX) * max_shift_amplitude;
    for (int i = 0; i < config.sampling_length_per_sample; i++) {
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
        mock_phase_offset = value - std::floor(value);
    } else if (key == "mock_missing_bin_start") {
        mock_missing_bin_start = static_cast<int>(value);
    } else if (key == "mock_missing_bin_count") {
        mock_missing_bin_count = static_cast<int>(value);
    } else {
        return false;
    }

    return true;
}

std::string MockSampler::name() { return "mock_sampler"; }

} // namespace Sampler
