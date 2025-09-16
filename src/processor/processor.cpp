#include <map>
#include <cmath>
#include <algorithm>
#include "processor.hpp"
#include "../constant.hpp"
#include "../global/global.hpp"
#include "../estimate/estimate.hpp"

namespace Commander {
    namespace Processor {

        bool align(const Config::SamplingConfig &config, Result::SamplingResult &result) {
            result.maximum = *std::max_element(result.totalSamplingBuffer.begin(), result.totalSamplingBuffer.end());
            result.minimum = *std::min_element(result.totalSamplingBuffer.begin(), result.totalSamplingBuffer.end());

            if (result.maximum - result.minimum < Constant::MinVoltageAmplitude) {
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
        bool summation(const Config::SamplingConfig &config, Result::SamplingResult &result) {
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
                const double &value = result.totalSamplingBuffer[i];

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

        Waveform average(const Config::SamplingConfig &config, Result::SamplingResult &result, const double frequency) {
            int resultWaveLength = int(config.sampling_frequency / frequency / 2);
            resultWaveLength = std::min(resultWaveLength, config.valid_length);

            int merged_size = resultWaveLength / Constant::MaxAverageSize + 1;
            int merged_length = resultWaveLength / merged_size;
            double interval = config.sampling_interval * merged_size;

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
            const int rapidDeclineCheckPoints = static_cast<int>(merged_length * Constant::RapidDeclineCheckPointsPercentage);
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
                                if (current_value - (*merged_wave)[i + rapidDeclineCheckPoints + j] >
                                    rapid_decline_threshold) {
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

        bool estimate(const Config::SamplingConfig &config, Result::SamplingResult &result) {
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
            }
            return true;
        }

    } // namespace Processor
} // namespace Commander
