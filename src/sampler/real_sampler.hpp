#ifndef REAL_SAMPLER_HPP
#define REAL_SAMPLER_HPP

#include "sampler.hpp"

#include "Windows.h"
#include "../daq_headers/legacy/bdaqctrl.h"
#include "../error/error.hpp"
#include <chrono>
#include <fstream>
#include <thread>
using namespace std;
using namespace Automation::BDaq;

#define deviceDescription L"PCI-1714,BID#0"
#define check_code(code)                                                                                               \
    if (BioFailed(code)) {                                                                                             \
        result.error_code = static_cast<Error::Code>(code);                                                            \
        return false;                                                                                                  \
    }

namespace Sampler {

class RealSampler : public Sampler {
  public:
    bool sample(const Config::SamplingConfig& config, Result::SamplingResult& result) override {
        return config.is_instant() ? sample_instant(config, result) : sample_buffered(config, result);
    }

    double get_value(const std::string& key) override { return 0.0; }

    bool set_value(const std::string& key, const double value) override { return true; }

    std::string name() override { return "real_sampler"; }

  private:
    bool sample_buffered(const Config::SamplingConfig& config, Result::SamplingResult& result) {
        ErrorCode code = Success;
        BufferedAiCtrl* bfdAiCtrl = AdxBufferedAiCtrlCreate();
        if (!bfdAiCtrl) {
            result.error_code = Error::ErrorHandleNotValid;
            return false;
        }
        const auto fail = [&](ErrorCode value) {
            if (BioFailed(value)) {
                result.error_code = static_cast<Error::Code>(value);
                bfdAiCtrl->Dispose();
                return true;
            }
            return false;
        };

        // connect device
        DeviceInformation devInfo(deviceDescription);
        code = bfdAiCtrl->setSelectedDevice(devInfo);
        if (fail(code))
            return false;

        // dynamic range setting
        AiChannelCollection* channels = bfdAiCtrl->getChannels();
        channels->getItem(0).setValueRange(V_Neg5To5);

        // sampling rate setting
        ConvertClock* convertClock = bfdAiCtrl->getConvertClock();
        convertClock->setRate(config.sampling_frequency);

        // sampling data
        ScanChannel* scanChannel = bfdAiCtrl->getScanChannel();
        code = scanChannel->setChannelStart(0);
        if (fail(code))
            return false;
        code = scanChannel->setChannelCount(1);
        if (fail(code))
            return false;
        code = scanChannel->setSamples(config.sampling_length_per_sample);
        if (fail(code))
            return false;
        code = bfdAiCtrl->Prepare();
        if (fail(code))
            return false;
        for (int i = 0; i < config.sampling_time; ++i) {
            code = bfdAiCtrl->RunOnce();
            if (fail(code))
                return false;
            code = bfdAiCtrl->GetData(config.sampling_length_per_sample,
                                      result.totalSamplingBuffer.data() + config.sampling_length_per_sample * i);
            if (fail(code))
                return false;
        }

        bfdAiCtrl->Dispose();

        dump_origin_data(config, result);

        return true;
    }

    bool sample_instant(const Config::SamplingConfig& config, Result::SamplingResult& result) {
        InstantAiCtrl* controller = AdxInstantAiCtrlCreate();
        if (!controller) {
            result.error_code = Error::ErrorHandleNotValid;
            return false;
        }
        DeviceInformation device(deviceDescription);
        ErrorCode code = controller->setSelectedDevice(device);
        if (BioFailed(code)) {
            result.error_code = static_cast<Error::Code>(code);
            controller->Dispose();
            return false;
        }
        AiChannelCollection* channels = controller->getChannels();
        channels->getItem(0).setValueRange(V_Neg5To5);

        const InstantAi::Schedule schedule =
            InstantAi::build_schedule(config.emitting_frequency, config.instant_ai_target_points_per_waveform,
                                      config.instant_ai_min_read_interval_seconds);
        result.instant_ai_waveforms.clear();
        std::size_t sample_index = 0;
        for (int waveform = 0; waveform < config.number_of_waveforms; ++waveform) {
            InstantAi::TimedWaveform readings;
            const auto origin = std::chrono::steady_clock::now();
            for (double planned : schedule.planned_seconds) {
                std::this_thread::sleep_until(origin + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                           std::chrono::duration<double>(planned)));
                const auto started = std::chrono::steady_clock::now();
                double voltage = 0.0;
                code = controller->ReadAny(0, 1, nullptr, &voltage);
                if (BioFailed(code)) {
                    result.error_code = static_cast<Error::Code>(code);
                    controller->Dispose();
                    return false;
                }
                const double actual = std::chrono::duration<double>(started - origin).count();
                if (actual > schedule.deadline_seconds) {
                    result.error_code = Error::INSTANT_AI_SCHEDULE_TIMEOUT;
                    controller->Dispose();
                    return false;
                }
                readings.push_back({planned, actual, voltage});
                if (sample_index < result.totalSamplingBuffer.size()) {
                    result.totalSamplingBuffer[sample_index++] = voltage;
                }
            }
            result.instant_ai_waveforms.push_back(readings);
        }
        controller->Dispose();
        return dump_origin_data(config, result);
    }
};

} // namespace Sampler

#endif
