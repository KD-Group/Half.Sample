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

    bool MockSampler::sample(const Config::SamplingConfig &config, Result::SamplingResult &result) {
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
                        current[j] +=
                                w / 3 * exp((config.waveform_length / 2 - j) * config.sampling_interval / -mock_tau);
                    }
                } else if (j < config.waveform_length / 2 + transition_length) {
                    current[j] =
                            (transition_length - (j - config.waveform_length / 2)) * mock_v_inf / transition_length;
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

    double MockSampler::get_value(const std::string &key) {
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

    bool MockSampler::set_value(const std::string &key, const double value) {
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

    std::string MockSampler::name() {
        return "mock_sampler";
    }

} // namespace Sampler
