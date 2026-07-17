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
}

bool MockSampler::sample(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.is_instant()) {
        result.instant_ai_waveforms.clear();
        std::size_t sample_index = 0;
        const auto schedule =
            InstantAi::build_schedule(config.emitting_frequency, config.instant_ai_target_points_per_waveform,
                                      config.instant_ai_min_read_interval_seconds);
        for (int waveform = 0; waveform < config.number_of_waveforms; ++waveform) {
            InstantAi::TimedWaveform readings;
            for (double planned : schedule.planned_seconds) {
                const double cycles = planned * config.emitting_frequency;
                const double phase = cycles - std::floor(cycles);
                double voltage = 0.0;
                if (phase < 0.5) {
                    const double time_us = phase / config.emitting_frequency * 1e6;
                    voltage = mock_v_inf + (mock_v0 - mock_v_inf) * std::exp(time_us / -mock_tau);
                    if (mock_is_going_down) {
                        voltage += (mock_v0 - mock_v_inf) / 3 *
                                   std::exp((0.5 / config.emitting_frequency * 1e6 - time_us) / -mock_tau);
                    }
                }
                const double actual = planned;
                readings.push_back({planned, actual, voltage});
                if (sample_index < result.totalSamplingBuffer.size()) {
                    result.totalSamplingBuffer[sample_index++] = voltage;
                }
            }
            result.instant_ai_waveforms.push_back(readings);
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
    } else {
        return false;
    }

    return true;
}

std::string MockSampler::name() { return "mock_sampler"; }

} // namespace Sampler
