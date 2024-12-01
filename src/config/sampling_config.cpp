#include "sampling_config.hpp"
#include "../constant.hpp"

namespace Config {

    void SamplingConfig::update(int waveforms, double frequency) {
        this->number_of_waveforms = waveforms;
        this->emitting_frequency = frequency;

        if (frequency >= Constant::HighSpeedSamplingThreshold) {
            this->sampling_frequency = Constant::MaxSamplingFrequency;
        } else {
            this->sampling_frequency = Constant::MinSamplingFrequency;
        }

        this->sampling_interval = 1e6 / this->sampling_frequency;
        this->waveform_length = int(sampling_frequency / frequency);
        this->sampling_length = this->waveform_length * (waveforms + 1);

        int cropped_length = Constant::CroppedLength * this->sampling_frequency / Constant::MaxSamplingFrequency;
        this->valid_length = waveform_length / 2 - cropped_length;
    }

} // namespace Samper
