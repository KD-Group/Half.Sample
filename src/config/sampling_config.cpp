#include "sampling_config.hpp"
#include "../commander/base.hpp"
#include "../constant.hpp"

#include <algorithm>

namespace Config {

    void SamplingConfig::update(int waveforms, double frequency) {
        number_of_waveforms = waveforms;
        emitting_frequency = frequency;

        if (frequency >= Constant::HighSpeedSamplingThreshold) {
            sampling_frequency = Constant::MaxSamplingFrequency;
        } else {
            sampling_frequency = Constant::MinSamplingFrequency;
        }

        // 1. 如果设置的发射频率超过10Hz，采集卡采样频率设置为2e7，否则为1e6
        // 2. 采样间隔为1e6 / 采集卡频率，即当采样频率为2e7的时候，采样间隔为0.05，采样频率为1e6的时候，采样间隔为1
        // 3. 波形长度为采集卡频率(sampling_frequency)/发射频率(emitting_frequency)，因为发射频率代表1s发送N个波形，采集卡频率代表1s采集M个点，这M个点里包含N个完整的波形，所以每个波形的长度为M / N个点
        // 4. 如果算出波形长度(wave_form_length)，当采样平均次数(waveforms)为，那么需要wave_form_length * waveforms个点，
        //    这个时候根据采集卡最大采样点数除以波形长度，即MaxSamplingFrequency / wave_form_length，这个时候计算方式如下
        //
        //  采集卡一次采集能拿到的完整波形个数为waveforms_per_sampling = MaxSamplingFrequency / wave_form_length, 采集卡采样次数为N，则
        //	sampling_time = (waveforms / waveforms_per_sample) + 1
        //  举一个例子，采集卡一次能拿到完整波形为5，如果波形平均次数为32，那么要采集 (32 / 5) + 1 = 7次
        sampling_interval = 1e6 / sampling_frequency;
        Commander::Base::variable(sampling_interval);
        waveform_length = int(sampling_frequency / frequency);
        Commander::Base::variable(waveform_length);
        sampling_length_per_sample = std::min(Constant::MaxSamplingPoints, waveform_length * (number_of_waveforms + 1));
        Commander::Base::variable(sampling_length_per_sample);
        waveforms_per_sample = std::max(1, sampling_length_per_sample / waveform_length - 1);
        Commander::Base::variable(waveforms_per_sample);
        sampling_time = int(number_of_waveforms / waveforms_per_sample) + 1;
        Commander::Base::variable(sampling_time);
        int cropped_length = int(Constant::CroppedLength * sampling_frequency / Constant::MaxSamplingFrequency);
        valid_length = waveform_length / 2 - cropped_length;
        valid_length = valid_length > cropped_length ? valid_length : waveform_length;
        Commander::Base::variable(valid_length);
    }

} // namespace Sampler
