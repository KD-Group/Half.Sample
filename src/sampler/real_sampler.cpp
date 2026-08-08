#define NOMINMAX
#include "real_sampler.hpp"
#include "controller_owner.hpp"
#include "instant_acquisition.hpp"

#include "Windows.h"
#include "../daq_headers/legacy/bdaqctrl.h"
#include "../error/error.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

using namespace Automation::BDaq;

namespace {
const wchar_t* const device_description = L"PCI-1714,BID#0";

class RealInstantPlatform : public Sampler::InstantAi::InstantAcquisitionPlatform {
  public:
    RealInstantPlatform(InstantAiCtrl* controller, Result::SamplingResult& result)
        : controller_(controller), result_(result), origin_(std::chrono::steady_clock::now()) {}

    bool wait_until(double planned_seconds) override {
        const auto target = origin_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(planned_seconds));
        while (std::chrono::steady_clock::now() < target) {
            if (result_.progress.cancel_requested.load(std::memory_order_acquire)) return false;
            const auto chunk = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
            result_.progress.wait_until_or_cancel(std::min(target, chunk));
            result_.progress.update_elapsed(now_seconds());
        }
        return !result_.progress.cancel_requested.load(std::memory_order_acquire);
    }
    double now_seconds() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin_).count();
    }
    int read(double& voltage) override {
        // The vendor's synchronous ReadAny call has no cancellation API.
        return static_cast<int>(controller_->ReadAny(0, 1, nullptr, &voltage));
    }
    bool read_failed(int code) const override { return BioFailed(static_cast<ErrorCode>(code)); }

  private:
    InstantAiCtrl* controller_;
    Result::SamplingResult& result_;
    std::chrono::steady_clock::time_point origin_;
};
}

namespace Sampler {
bool RealSampler::sample(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    return config.is_instant() ? sample_instant(config, result) : sample_buffered(config, result);
}
double RealSampler::get_value(const std::string&) { return 0.0; }
bool RealSampler::set_value(const std::string&, double) { return true; }
std::string RealSampler::name() { return "real_sampler"; }

bool RealSampler::sample_buffered(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    ErrorCode code = Success;
    ControllerOwner<BufferedAiCtrl> controller(AdxBufferedAiCtrlCreate());
    if (!controller.get()) { result.error_code = Error::ErrorHandleNotValid; return false; }
    const auto fail = [&](ErrorCode value) {
        if (BioFailed(value)) { result.error_code = static_cast<Error::Code>(value); return true; }
        return false;
    };
    DeviceInformation devInfo(device_description);
    code = controller->setSelectedDevice(devInfo); if (fail(code)) return false;
    controller->getChannels()->getItem(0).setValueRange(V_Neg5To5);
    controller->getConvertClock()->setRate(config.sampling_frequency);
    ScanChannel* scanChannel = controller->getScanChannel();
    code = scanChannel->setChannelStart(0); if (fail(code)) return false;
    code = scanChannel->setChannelCount(1); if (fail(code)) return false;
    code = scanChannel->setSamples(config.sampling_length_per_sample); if (fail(code)) return false;
    code = controller->Prepare(); if (fail(code)) return false;
    for (int i = 0; i < config.sampling_time; ++i) {
        code = controller->RunOnce(); if (fail(code)) return false;
        code = controller->GetData(config.sampling_length_per_sample,
            result.totalSamplingBuffer.data() + config.sampling_length_per_sample * i);
        if (fail(code)) return false;
    }
    controller.reset();
    dump_origin_data(config, result);
    return true;
}

bool RealSampler::sample_instant(const Config::SamplingConfig& config, Result::SamplingResult& result) {
    ControllerOwner<InstantAiCtrl> controller(AdxInstantAiCtrlCreate());
    if (!controller.get()) {
        result.instant_ai_actual_duration_seconds = 0.0;
        result.error_code = Error::ErrorHandleNotValid;
        return false;
    }
    DeviceInformation device(device_description);
    ErrorCode code = controller->setSelectedDevice(device);
    if (BioFailed(code)) {
        result.instant_ai_actual_duration_seconds = 0.0;
        result.error_code = static_cast<Error::Code>(code);
        return false;
    }
    controller->getChannels()->getItem(0).setValueRange(V_Neg5To5);

    const InstantAi::ContinuousSchedule schedule = InstantAi::build_continuous_schedule(
        config.emitting_frequency, config.number_of_waveforms,
        config.instant_ai_target_points_per_waveform, config.instant_ai_max_reliable_polling_hz);
    RealInstantPlatform platform(controller.get(), result);
    if (!InstantAi::run_continuous_acquisition(schedule, config.number_of_waveforms + 1, platform, result))
        return false;
    return dump_origin_data(config, result);
}
} // namespace Sampler
