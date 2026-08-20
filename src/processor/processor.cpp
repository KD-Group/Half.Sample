#include <map>
#include <cmath>
#include <algorithm>
#include <iterator>
#include "processor.hpp"
#include "independent_cycle.hpp"
#include "../constant.hpp"
#include "../estimate/estimate.hpp"

namespace Commander {
namespace Processor {

namespace {

bool record_voltage(Result::SamplingResult& result, double voltage) {
    if (std::isfinite(voltage) && voltage > 0.0) {
        result.v_inf = voltage;
        result.v_inf_valid = true;
        return true;
    }
    result.v_inf = 0.0;
    result.v_inf_valid = false;
    return false;
}

bool fit_is_identifiable(const Waveform& wave, const Estimate::EstimatedResult& estimate) {
    if (!wave.values || wave.values->size() < 2 ||
        !std::isfinite(wave.interval) || wave.interval <= 0.0 ||
        !std::isfinite(estimate.tau) || !std::isfinite(estimate.w) ||
        !std::isfinite(estimate.b)) {
        return false;
    }

    // A tau at the search boundary is an indication that the acquired window
    // contains too little exponential evolution to identify tau.
    if (estimate.tau >= Constant::MaxTauValue * 0.99) {
        return false;
    }

    const double duration = wave.interval * static_cast<double>(wave.values->size() - 1);
    const double normalized_change = 1.0 - std::exp(-duration / estimate.tau);
    if (!std::isfinite(normalized_change) || normalized_change < 0.05) {
        return false;
    }
    return true;
}

} // namespace

bool validate_finite_result(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (!result.success) {
        if (!result.v_inf_valid || !std::isfinite(result.v_inf) || result.v_inf <= 0.0) {
            result.v_inf = 0.0;
            result.v_inf_valid = false;
        }
        return false;
    }

    const Estimate::EstimatedResult& estimate = result.estimate;
    const bool finite_scalars = result.v_inf_valid && result.v_inf > 0.0 &&
                                std::isfinite(result.maximum) && std::isfinite(result.minimum) &&
                                std::isfinite(result.cycle_maximum) && std::isfinite(result.cycle_minimum) &&
                                std::isfinite(result.v_inf) &&
                                std::isfinite(config.sampling_interval) &&
                                std::isfinite(estimate.interval) && std::isfinite(estimate.tau) &&
                                std::isfinite(estimate.w) && std::isfinite(estimate.b) &&
                                std::isfinite(estimate.loss);
    const bool finite_wave = !estimate.y ||
                             std::all_of(estimate.y->begin(), estimate.y->end(), [](double value) {
                                 return std::isfinite(value);
                             });
    if (finite_scalars && finite_wave)
        return true;

    result.success = false;
    result.error_code = Error::SAMPLING_RESULT_NOT_FINITE;
    result.maximum = 0.0;
    result.minimum = 0.0;
    result.cycle_maximum = 0.0;
    result.cycle_minimum = 0.0;
    result.v_inf = 0.0;
    result.v_inf_valid = false;
    result.estimate = Estimate::EstimatedResult();
    return false;
}

bool align(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    result.v_inf = 0.0;
    result.v_inf_valid = false;
    if (config.is_instant() && result.instant_ai_format_version >= 2) {
        bool found_valid_reading = false;
        for (const auto& reading : result.instant_ai_readings) {
            if (!reading.read_success || !std::isfinite(reading.voltage)) {
                continue;
            }
            if (!found_valid_reading) {
                result.minimum = reading.voltage;
                result.maximum = reading.voltage;
                found_valid_reading = true;
            } else {
                result.minimum = std::min(result.minimum, reading.voltage);
                result.maximum = std::max(result.maximum, reading.voltage);
            }
        }
        if (!found_valid_reading) {
            result.error_code = Error::VOLTAGE_NOT_ENOUGH;
            return false;
        }
    } else if (result.totalSamplingBuffer.empty()) {
        result.error_code = config.is_instant() ? Error::INSTANT_AI_COVERAGE_INSUFFICIENT : Error::WAVE_NOT_FOUND;
        return false;
    } else {
        result.maximum = *std::max_element(result.totalSamplingBuffer.begin(), result.totalSamplingBuffer.end());
        result.minimum = *std::min_element(result.totalSamplingBuffer.begin(), result.totalSamplingBuffer.end());
    }

    if (!record_voltage(result, result.maximum - result.minimum)) {
        result.error_code = Error::VOLTAGE_NOT_ENOUGH;
        return false;
    }
    if (result.v_inf < Constant::MinVoltageAmplitude) {
        result.error_code = Error::VOLTAGE_NOT_ENOUGH;
        return false;
    }

    return true;
}

/*
            寻找并叠加有效波形：
            - 遍历 result.totalSamplingBuffer 中的数据点
            - 使用状态机方式检测满足条件的波形起始点（低于下边界后又高于上边界）
            - 每次检测到完整波形时：
              - 累加波形前半部分作为基准值计算（用于后续去直流分量）
              - 将有效波形段叠加到 result.resultWave中

            关键处理步骤
            1. 波形检测逻辑：
                1.1 初始状态为 under_lower_bound = false
                1.2 当信号降至下边界以下时设置标志
                1.3 当信号升至上边界以上时认为检测到完整波形
                1.4 如果后续RapidDeclineCheckPoints个点中，百分之的RapidDeclineCheckPoints个点的波形下降速度超过RapidDeclinePercentage，
            2. 数据叠加与统计：
                2.1 base_sum 和 base_count 用于统计波形前导部分的平均值
                2.2 copy_times 统计叠加次数
                2.3 叠加多个周期的波形以提高信噪比
            3.错误处理：
                3.1 如果未检测到任何有效波形（copy_times == 0），返回错误码 Error::WAVE_NOT_FOUND
         */
bool summation(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.is_instant()) {
        const auto reconstructed = result.instant_ai_format_version >= 2
                                       ? Sampler::InstantAi::reconstruct_continuous(
                                             result.instant_ai_readings, config.number_of_waveforms,
                                             config.emitting_frequency, config.instant_ai_target_points_per_waveform)
                                       : Sampler::InstantAi::reconstruct_legacy_waveforms(
                                             result.instant_ai_waveforms, config.number_of_waveforms,
                                             config.emitting_frequency, config.instant_ai_target_points_per_waveform);
        if (!reconstructed.success) {
            switch (reconstructed.status) {
            case Sampler::InstantAi::ReconstructionStatus::AlignmentFailed:
                result.error_code = Error::INSTANT_AI_ALIGNMENT_FAILED;
                break;
            case Sampler::InstantAi::ReconstructionStatus::WaveformCountInsufficient:
                result.error_code = Error::INSTANT_AI_WAVEFORM_COUNT_INSUFFICIENT;
                break;
            case Sampler::InstantAi::ReconstructionStatus::CoverageInsufficient:
                result.error_code = Error::INSTANT_AI_COVERAGE_INSUFFICIENT;
                break;
            case Sampler::InstantAi::ReconstructionStatus::Success:
                result.error_code = Error::INSTANT_AI_ALIGNMENT_FAILED;
                break;
            }
            return false;
        }
        result.instant_ai_complete_waveforms = reconstructed.complete_waveforms;
        result.instant_ai_late_reads = reconstructed.late_reads;
        result.instant_ai_interpolated_bins = reconstructed.interpolated_bins;
        result.resultWave = reconstructed.averaged_half_wave;
        return true;
    }

    if (config.waveform_processing_mode == "independent_cycle") {
        result.requested_waveforms = config.number_of_waveforms;
        const std::size_t block_size = config.sampling_length_per_sample > 0
            ? static_cast<std::size_t>(config.sampling_length_per_sample)
            : result.totalSamplingBuffer.size();
        const std::size_t configured_batches = config.sampling_time > 0
            ? static_cast<std::size_t>(config.sampling_time)
            : 1u;
        for (std::size_t batch = 0; batch < configured_batches; ++batch) {
            if (result.independent_cycle_accumulator.size() >=
                static_cast<std::size_t>(config.number_of_waveforms))
                break;
            const std::size_t begin = batch * block_size;
            if (begin >= result.totalSamplingBuffer.size()) break;
            const std::size_t end = std::min(begin + block_size, result.totalSamplingBuffer.size());
            const int remaining = config.number_of_waveforms -
                static_cast<int>(result.independent_cycle_accumulator.size());
            auto independent = extract_independent_cycles(
                result.totalSamplingBuffer, begin, end,
                static_cast<int>(config.sampling_frequency),
                config.emitting_frequency, remaining);
            result.discarded_waveforms += independent.discarded_waveforms;
            result.rejected_threshold_candidates += independent.rejected_threshold_candidates;
            result.rejected_short_periods += independent.rejected_short_periods;
            result.rejected_long_periods += independent.rejected_long_periods;
            result.rejected_amplitude_cycles += independent.rejected_amplitude_cycles;
            result.rejected_baseline_cycles += independent.rejected_baseline_cycles;
            result.rejected_shape_cycles += independent.rejected_shape_cycles;
            result.independent_batches += 1;
            result.independent_cycle_accumulator.insert(
                result.independent_cycle_accumulator.end(),
                std::make_move_iterator(independent.cycles.begin()),
                std::make_move_iterator(independent.cycles.end()));
            result.independent_cycle_maximum_accumulator.insert(
                result.independent_cycle_maximum_accumulator.end(),
                independent.cycle_maximums.begin(), independent.cycle_maximums.end());
            result.independent_cycle_minimum_accumulator.insert(
                result.independent_cycle_minimum_accumulator.end(),
                independent.cycle_minimums.begin(), independent.cycle_minimums.end());
        }
        result.complete_waveforms = static_cast<int>(result.independent_cycle_accumulator.size());
        if (result.independent_cycle_accumulator.size() < static_cast<std::size_t>(config.number_of_waveforms)) {
            result.error_code = Error::INSTANT_AI_WAVEFORM_COUNT_INSUFFICIENT;
            return false;
        }

        double maximum_sum = 0.0;
        double minimum_sum = 0.0;
        for (int cycle = 0; cycle < config.number_of_waveforms; ++cycle) {
            maximum_sum += result.independent_cycle_maximum_accumulator[static_cast<std::size_t>(cycle)];
            minimum_sum += result.independent_cycle_minimum_accumulator[static_cast<std::size_t>(cycle)];
        }
        result.cycle_maximum = maximum_sum / static_cast<double>(config.number_of_waveforms);
        result.cycle_minimum = minimum_sum / static_cast<double>(config.number_of_waveforms);
        if (!record_voltage(result, result.cycle_maximum - result.cycle_minimum)) {
            result.error_code = Error::SAMPLING_RESULT_NOT_FINITE;
            return false;
        }

        for (int point = 0; point < config.valid_length; ++point) {
            double sum = 0.0;
            for (int cycle = 0; cycle < config.number_of_waveforms; ++cycle) {
                sum += result.independent_cycle_accumulator[static_cast<std::size_t>(cycle)]
                                                                  [static_cast<std::size_t>(point)];
            }
            result.resultWave[static_cast<std::size_t>(point)] =
                sum / static_cast<double>(config.number_of_waveforms);
        }
        return true;
    }

    const double minimum = result.minimum;
    const double maximum = result.maximum;

    const double current_lower_bound = minimum + (maximum - minimum) * Constant::LowerBound;
    const double current_upper_bound = minimum + (maximum - minimum) * Constant::UpperBound;

    for (int j = 0; j < config.valid_length; j++) {
        result.resultWave[j] = 0;
    }

    double base_sum = 0;
    int base_count = 0;

    int copy_times = 0;
    bool under_lower_bound = false;
    for (int i = Constant::CroppedLength; i < result.totalSamplingBuffer.size(); i++) {
        const double& value = result.totalSamplingBuffer[i];

        if (under_lower_bound) {
            if (value >= current_upper_bound) {
                under_lower_bound = false;

                for (int j = 0; j < Constant::CroppedLength / 2; j++) {
                    base_sum += result.totalSamplingBuffer[i - Constant::CroppedLength + j];
                    base_count++;
                }

                if (i + config.valid_length < result.totalSamplingBuffer.size()) {
                    for (int j = 0; j < config.valid_length; j++) {
                        result.resultWave[j] += result.totalSamplingBuffer[i++];
                    }
                    copy_times++;
                }
            }
        } else if (value <= current_lower_bound) {
            under_lower_bound = true;
        }
    }

    if (copy_times == 0) {
        result.error_code = Error::WAVE_NOT_FOUND;
        return false;
    }

    const double base_value = base_count ? base_sum / base_count : 0;
    for (int j = 0; j < config.valid_length; j++) {
        result.resultWave[j] = result.resultWave[j] / copy_times - base_value;
    }
    return true;
}

Waveform average(const Config::SamplingConfig& config, Result::SamplingResult& result, const double frequency) {
    const bool independent_cycle = config.waveform_processing_mode == "independent_cycle";
    int resultWaveLength = int(config.sampling_frequency / frequency / (independent_cycle ? 1 : 2));
    resultWaveLength = std::min(resultWaveLength, config.valid_length);

    int merged_size = resultWaveLength / Constant::MaxAverageSize + 1;
    int merged_length = resultWaveLength / merged_size;
    double interval = config.sampling_interval * merged_size;
    result.estimate.interval = interval;

    VectorPtr merged_wave(new Vector(merged_length));
    for (int i = 0; i < merged_length; i++) {
        int current = i * merged_size;

        double sum = 0;
        for (int j = 0; j < merged_size; j++) {
            sum += result.resultWave[current + j];
        }

        (*merged_wave)[i] = sum / merged_size;
    }

    auto maximum = *std::max_element(result.resultWave.begin(), result.resultWave.end());
    auto minimum = *std::min_element(result.resultWave.begin(), result.resultWave.end());
    const int rapidDeclineCheckPoints =
        std::max(1, static_cast<int>(merged_length * Constant::RapidDeclineCheckPointsPercentage));
    // 检查result.resultWave中是否存在快速下降的点
    int rapid_decline_point_idx = -1;
    if (maximum > minimum && merged_length > rapidDeclineCheckPoints) {
        // 计算快速下降阈值：(maximum - minimum) * 百分比
        const double rapid_decline_threshold = (maximum - minimum) * Constant::RapidDeclinePercentage;

        // 在result.resultWave中检测快速下降点
        for (int i = 0; i < merged_length - rapidDeclineCheckPoints; i++) {
            // 检查当前点和其后第N个点的下降幅度
            if ((i + rapidDeclineCheckPoints) < merged_length) {
                double current_value = (*merged_wave)[i];
                double next_value = (*merged_wave)[i + rapidDeclineCheckPoints];

                // 如果当前点相比后面第N个点的下降幅度超过阈值
                if (current_value - next_value > rapid_decline_threshold) {
                    // 检查第N个点后的N个点中是否有足够比例的点比当前点降幅大于阈值
                    int decrease_count = 0;
                    int total_check_points = 0;

                    // 检查第N个点后的N个点
                    for (int j = 1; j <= rapidDeclineCheckPoints; j++) {
                        // 边界检查
                        if ((i + rapidDeclineCheckPoints + j) >= merged_length) {
                            break; // 超出边界则停止检查
                        }

                        total_check_points++;
                        if (current_value - (*merged_wave)[i + rapidDeclineCheckPoints + j] > rapid_decline_threshold) {
                            decrease_count++;
                        }
                    }

                    // 如果满足比例的点满足条件，则记录截取点
                    if (total_check_points > 0 &&
                        static_cast<double>(decrease_count) / total_check_points >= Constant::RapidDeclineThreshold) {
                        // 记录快速下降点的索引
                        rapid_decline_point_idx = i;
                        break; // 找到第一个满足条件的点就停止
                    }
                }
            }
        }
    }
    return {merged_wave, interval, rapid_decline_point_idx};
}

bool estimate(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    if (config.auto_mode) {
        std::map<double, Estimate::EstimatedResult> results;

        double frequency_upper_bound = Constant::HighSpeedEstimatedFrequencyUpperBound;
        if (config.emitting_frequency < Constant::HighSpeedSamplingThreshold) {
            frequency_upper_bound = Constant::LowSpeedEstimatedFrequencyUpperBound;
        }

        for (double frequency = config.emitting_frequency; frequency <= frequency_upper_bound; frequency *= 2) {
            auto wave = average(config, result, frequency);
            if (Estimate::is_wave_going_down(wave)) {
                continue;
            }

            auto estimated_result = Estimate::one_third_search(wave);
            auto y = estimated_result.y;

            double a = -estimated_result.w;
            double b = estimated_result.margin();

            if (a <= 0 || b <= 0 || estimated_result.tau < Constant::MinTauValue) {
                continue;
            }

            double f1_score = a * b / (a + b);
            results[-f1_score] = estimated_result;
        }

        if (results.empty()) {
            result.error_code = Error::APPROPRIATE_WAVE_NOT_FOUND;
            return false;
        }

        result.estimate = results.begin()->second;
    } else {
        auto wave = average(config, result, config.emitting_frequency);
        result.estimate = Estimate::one_third_search(wave);
        if (!config.is_instant() && config.waveform_processing_mode == "independent_cycle" &&
            !fit_is_identifiable(wave, result.estimate)) {
            result.estimate = Estimate::EstimatedResult();
            result.error_code = Error::FIT_NOT_IDENTIFIABLE;
            return false;
        }
    }
    if (config.is_instant() || config.waveform_processing_mode != "independent_cycle")
        record_voltage(result, result.estimate.b);
    return true;
}

} // namespace Processor
} // namespace Commander
