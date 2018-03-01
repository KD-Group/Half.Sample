#include <cmath>
#include <algorithm>
#include "processer.hpp"
#include "../constant.hpp"
#include "../global/global.hpp"
#include "../estimate/estimate.hpp"

namespace Commander {
namespace Processer {

bool align(const Sampler::SamplerConfig &config, Result::SamplingResult &result) {
    result.maximum = *std::max_element(result.buffer, result.buffer + config.sampling_length);
    result.minimum = *std::min_element(result.buffer, result.buffer + config.sampling_length);

    if (result.maximum - result.minimum < Constant::MinVoltageAmplitude) {
        Global::message = "voltage_not_enough";
        return false;
    }

    return true;
}

bool summation(const Sampler::SamplerConfig &config, Result::SamplingResult &result) {
    const double minimum = result.minimum;
    const double maximum = result.maximum;

    const double current_lower_bound = minimum + (maximum - minimum) * Constant::LowerBound;
    const double current_upper_bound = minimum + (maximum - minimum) * Constant::UpperBound;

    for (int j = 0; j < config.valid_length; j ++) {
        result.wave[j] = 0;
    }

    double base_sum = 0;
    int base_count = 0;

    int copy_times = 0;
    bool under_lower_bound = false;
    for (int i = Constant::CroppedLength; i < config.sampling_length; i ++) {
        const double &value = result.buffer[i];

        if (under_lower_bound) {
            if (value >= current_upper_bound) {
                under_lower_bound = false;

                for (int j = 0; j < Constant::CroppedLength / 2; j ++) {
                    base_sum += result.buffer[i - Constant::CroppedLength + j];
                    base_count++;
                }

                if (i + config.valid_length < config.sampling_length) {
                    for (int j = 0; j < config.valid_length; j ++) {
                        result.wave[j] += result.buffer[i + j];
                    }
                    copy_times ++;
                }
            }
        } else if (value <= current_lower_bound) {
            under_lower_bound = true;
        }
    }

    const double base_value = base_count ? base_sum / base_count : 0;
    for (int j = 0; j < config.valid_length; j ++) {
        result.wave[j] = result.wave[j] / copy_times - base_value;
    }
    return true;
}

Waveform average(const Sampler::SamplerConfig &config, Result::SamplingResult &result, const double frequency) {
    int length = round(config.sampling_frequency / frequency / 2);
    length = std::min(length, config.valid_length);

    int merged_size = length / Constant::MaxAverageSize + 1;
    int merged_length = length / merged_size;
    double interval = config.sampling_interval * merged_size;

    VectorPtr merged_wave(new Vector(merged_length));
    for (int i = 0; i < merged_length; i++) {
        int current = i * merged_size;

        double sum = 0;
        for (int j = 0; j < merged_size; j++) {
            sum += result.wave[current + j];
        }

        (*merged_wave)[i] = sum / merged_size;
    }

    return Waveform({merged_wave, interval});
}

bool estimate(const Sampler::SamplerConfig &config, Result::SamplingResult &result) {
    if (Global::auto_mode) {

    } else {
        auto wave = average(config, result, config.emitting_frequency);
        result.estimate = Estimate::one_third_search(wave);
    }
    return true;
}

} // namespace Processer
} // namespace Commander
