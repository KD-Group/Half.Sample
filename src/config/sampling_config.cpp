#include "sampling_config.hpp"
#include "../commander/base.hpp"
#include "../constant.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace Config {

bool SamplingConfig::update(int waveforms, double frequency, double instant_threshold, int instant_target_points,
                            double instant_max_reliable_polling_hz, const std::string& processing_mode) {
    if (waveforms <= 0 || !std::isfinite(frequency) || frequency <= 0 ||
        !std::isfinite(instant_threshold) || instant_threshold < 0) {
        return false;
    }

    if (processing_mode != "threshold_accumulation" && processing_mode != "independent_cycle") {
        return false;
    }

    if (instant_threshold > 0 &&
        (instant_target_points < 20 || !std::isfinite(instant_max_reliable_polling_hz) ||
         instant_max_reliable_polling_hz <= 0 ||
         instant_threshold * instant_target_points >
             instant_max_reliable_polling_hz + 1e-12)) {
        return false;
    }

    const AcquisitionMode requested_mode =
        instant_threshold > 0 && frequency <= instant_threshold ? AcquisitionMode::Instant : AcquisitionMode::Buffered;
    double instant_polling_frequency = 0.0;
    double instant_planned_duration_seconds = 0.0;
    std::size_t instant_planned_readings = 0;
    int buffered_sampling_frequency = 0;
    int buffered_waveform_length = 0;
    int buffered_confirmation_points = 0;
    int buffered_sampling_length = 0;
    int buffered_waveforms_per_sample = 0;
    int buffered_sampling_time = 0;

    if (requested_mode == AcquisitionMode::Instant) {
        instant_polling_frequency = std::min(frequency * instant_target_points, instant_max_reliable_polling_hz);
        instant_planned_duration_seconds = (waveforms + 1.0) / frequency;
        const double instant_sampling_interval = 1e6 / instant_polling_frequency;
        const double unrounded_reading_count = instant_planned_duration_seconds * instant_polling_frequency;
        const double rounded_reading_count = std::round(unrounded_reading_count) + 1.0;
        if (!std::isfinite(instant_polling_frequency) || instant_polling_frequency <= 0 ||
            !std::isfinite(instant_planned_duration_seconds) || instant_planned_duration_seconds <= 0 ||
            !std::isfinite(instant_sampling_interval) || instant_sampling_interval <= 0 ||
            !std::isfinite(rounded_reading_count) || rounded_reading_count < 1 ||
            rounded_reading_count > Constant::MaxSamplingPoints) {
            return false;
        }
        instant_planned_readings = static_cast<std::size_t>(std::llround(unrounded_reading_count)) + 1;
        const std::size_t reconstruction_rows = static_cast<std::size_t>(waveforms) + 1;
        if (reconstruction_rows > Constant::MaxInstantAiReconstructionCells / instant_planned_readings) {
            return false;
        }
    } else {
        buffered_sampling_frequency = frequency >= Constant::HighSpeedSamplingThreshold
            ? Constant::MaxSamplingFrequency
            : Constant::MinSamplingFrequency;
        const double waveform_length_value = buffered_sampling_frequency / frequency;
        if (!std::isfinite(waveform_length_value) || waveform_length_value < 1.0 ||
            waveform_length_value > INT_MAX) {
            return false;
        }
        buffered_waveform_length = static_cast<int>(waveform_length_value);
        const long long requested_sampling_points =
            static_cast<long long>(buffered_waveform_length) *
                (static_cast<long long>(waveforms) + 1LL);
        buffered_sampling_length = static_cast<int>(std::min(
            static_cast<long long>(Constant::MaxSamplingPoints), requested_sampling_points));
        if (processing_mode == "independent_cycle") {
            const int complete_periods =
                buffered_sampling_length / buffered_waveform_length;
            buffered_waveforms_per_sample = complete_periods - 1;
            if (buffered_waveforms_per_sample < 1) return false;
        } else {
            buffered_waveforms_per_sample = std::max(
                1, buffered_sampling_length / buffered_waveform_length - 1);
        }
        const long long sampling_time_value =
            (static_cast<long long>(waveforms) + buffered_waveforms_per_sample - 1LL) /
            buffered_waveforms_per_sample;
        if (sampling_time_value < 1 || sampling_time_value > INT_MAX ||
            static_cast<long long>(buffered_sampling_length) * sampling_time_value >
                Constant::MaxBufferSize) {
            return false;
        }
        buffered_sampling_time = static_cast<int>(sampling_time_value);
    }

    number_of_waveforms = waveforms;
    waveform_processing_mode = processing_mode;
    emitting_frequency = frequency;
    instant_ai_frequency_threshold = instant_threshold;
    instant_ai_target_points_per_waveform = instant_target_points;
    instant_ai_max_reliable_polling_hz = instant_max_reliable_polling_hz;
    instant_ai_polling_frequency = instant_polling_frequency;
    instant_ai_planned_duration_seconds = instant_planned_duration_seconds;
    instant_ai_planned_readings = instant_planned_readings;
    acquisition_mode = requested_mode;

    if (is_instant()) {
        sampling_frequency = instant_ai_polling_frequency;
        sampling_interval = 1e6 / sampling_frequency;
        waveform_length = instant_target_points;
        valid_length = waveform_length / 2;
        sampling_length_per_sample = static_cast<int>(instant_ai_planned_readings);
        waveforms_per_sample = 1.0;
        sampling_time = 1;
        Commander::Base::variable(number_of_waveforms);
        Commander::Base::variable(sampling_interval);
        Commander::Base::variable(waveform_length);
        Commander::Base::variable(sampling_length_per_sample);
        Commander::Base::variable(waveforms_per_sample);
        Commander::Base::variable(sampling_time);
        Commander::Base::variable(valid_length);
        return true;
    }

    sampling_frequency = buffered_sampling_frequency;

    // 1. 如果设置的发射频率超过10Hz，采集卡采样频率设置为2e7，否则为1e6
    // 2. 采样间隔为1e6 / 采集卡频率，即当采样频率为2e7的时候，采样间隔为0.05，采样频率为1e6的时候，采样间隔为1
    // 3. 波形长度为采集卡频率(sampling_frequency)/发射频率(emitting_frequency)，因为发射频率代表1s发送N个波形，采集卡频率代表1s采集M个点，这M个点里包含N个完整的波形，所以每个波形的长度为M / N个点
    // 4. 如果算出波形长度(wave_form_length)，当采样平均次数(waveforms)为，那么需要wave_form_length * waveforms个点，
    //    这个时候根据采集卡最大采样点数除以波形长度，即MaxSamplingFrequency / wave_form_length，这个时候计算方式如下
    //
    //  采集卡一次采集能拿到的完整波形个数为waveforms_per_sampling = MaxSamplingFrequency / wave_form_length, 采集卡采样次数为N，则
    //	sampling_time = (waveforms / waveforms_per_sample) + 1
    //  举一个例子，采集卡一次能拿到完整波形为5，如果波形平均次数为32，那么要采集 (32 / 5) + 1 = 7次
    Commander::Base::variable(number_of_waveforms);
    sampling_interval = 1e6 / sampling_frequency;
    Commander::Base::variable(sampling_interval);
    waveform_length = buffered_waveform_length;
    Commander::Base::variable(waveform_length);
    sampling_length_per_sample = buffered_sampling_length;
    Commander::Base::variable(sampling_length_per_sample);
    waveforms_per_sample = buffered_waveforms_per_sample;
    Commander::Base::variable(waveforms_per_sample);
    sampling_time = buffered_sampling_time;
    Commander::Base::variable(sampling_time);
    int cropped_length = int(Constant::CroppedLength * sampling_frequency / Constant::MaxSamplingFrequency);
    if (waveform_processing_mode == "independent_cycle") {
        // Independent-cycle processing keeps a complete period.  The fitting
        // stage may select a valid monotonic part from that period later.
        valid_length = waveform_length;
    } else {
        valid_length = waveform_length / 2 - cropped_length;
        valid_length = valid_length > cropped_length ? valid_length : waveform_length;
    }
    Commander::Base::variable(valid_length);
    return true;
}

} // namespace Config
