#ifndef sampling_config_HPP
#define sampling_config_HPP

#include <string>

namespace Config {

    struct SamplingConfig {
        double sampling_frequency;  // Hz
        double emitting_frequency;  // Hz
        double sampling_interval;  // us

        int sampling_length;
        int waveform_length;
        int valid_length;
        int number_of_waveforms;

        bool auto_mode;

        std::string dump_file_path;

        void update(int waveforms, double frequency);
    };

} // namespace Sampler

#endif
