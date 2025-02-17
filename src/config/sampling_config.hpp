#ifndef sampling_config_HPP
#define sampling_config_HPP

#include <string>

namespace Config {

    struct SamplingConfig {
        double sampling_frequency;  // Hz
        double emitting_frequency;  // Hz
        double sampling_interval;  // us

        int waveforms_per_sample; // 采集卡每次采样时能得到的最大波形数量
        int sampling_time; // 采样次数,当要求的波形数量大于采集卡单次最大采样点数的时，进行多次采样直到能采集到要求的波形
        int sampling_length_per_sample; // 采集卡单次采样点数
        int waveform_length; // 一个完整波形的点数(包括上升沿和下降沿)
        int valid_length; // 有效的波形点数(仅包含上升沿部分)
        int number_of_waveforms; // 波形平均次数

        bool auto_mode;

        std::string dump_file_path;

        void update(int waveforms, double frequency);
    };

} // namespace Config

#endif
