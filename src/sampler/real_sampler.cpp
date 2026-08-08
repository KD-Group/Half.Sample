#define NOMINMAX
#include "real_sampler.hpp"
#include "controller_owner.hpp"

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

class CycleTracker {
  public:
    explicit CycleTracker(int target) : target_(target) {}
    int observe(double value) {
        minimum_ = std::min(minimum_, value);
        maximum_ = std::max(maximum_, value);
        const double span = maximum_ - minimum_;
        if (span < Constant::MinVoltageAmplitude) return cycles_;
        const double low = minimum_ + span * Constant::LowerBound;
        const double high = minimum_ + span * Constant::UpperBound;
        if (value <= low) armed_ = true;
        else if (armed_ && value >= high) {
            armed_ = false;
            cycles_ = std::min(target_, cycles_ + 1);
        }
        return cycles_;
    }
  private:
    int target_, cycles_ = 0;
    bool armed_ = false;
    double minimum_ = std::numeric_limits<double>::infinity();
    double maximum_ = -std::numeric_limits<double>::infinity();
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
    result.instant_ai_waveforms.clear();
    result.instant_ai_readings.clear();
    result.instant_ai_format_version = 2;
    result.instant_ai_readings.reserve(schedule.planned_seconds.size());
    const auto origin = std::chrono::steady_clock::now();
    const double deadline = schedule.duration_seconds + std::max(1.0, schedule.duration_seconds * 0.1);
    CycleTracker cycles(config.number_of_waveforms + 1);
    double previous_planned = 0.0;
    const auto finish = [&]() {
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count();
        result.instant_ai_actual_duration_seconds = elapsed;
        result.progress.update_elapsed(elapsed);
    };
    for (double planned : schedule.planned_seconds) {
        const auto target = origin + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(planned));
        while (std::chrono::steady_clock::now() < target) {
            if (result.progress.cancel_requested.load()) {
                result.error_code = Error::USER_CANCELLED; result.cancelled = true; finish(); return false;
            }
            const auto chunk = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
            result.progress.wait_until_or_cancel(std::min(target, chunk));
            const double waited =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count();
            result.progress.update_elapsed(waited);
        }
        if (result.progress.cancel_requested.load()) {
            result.error_code = Error::USER_CANCELLED; result.cancelled = true; finish(); return false;
        }
        const auto started = std::chrono::steady_clock::now();
        const double started_seconds = std::chrono::duration<double>(started - origin).count();
        double voltage = std::numeric_limits<double>::quiet_NaN();
        // The vendor's synchronous ReadAny call has no cancellation API. Cancellation is observed immediately after it.
        code = controller->ReadAny(0, 1, nullptr, &voltage);
        const auto completed = std::chrono::steady_clock::now();
        const double completed_seconds = std::chrono::duration<double>(completed - origin).count();
        const InstantAi::ReadTiming timing = InstantAi::evaluate_read_timing(
            planned, started_seconds, completed_seconds, previous_planned, deadline);
        const double actual = timing.actual_seconds;
        result.progress.update_elapsed(actual);
        if (timing.late) {
            result.progress.late_reads.fetch_add(1);
            result.instant_ai_late_reads++;
        }
        previous_planned = planned;
        if (BioFailed(code)) {
            result.instant_ai_readings.push_back({planned, actual, std::numeric_limits<double>::quiet_NaN(), false,
                                                  static_cast<int>(code)});
            result.error_code = static_cast<Error::Code>(code); finish(); return false;
        }
        if (result.progress.cancel_requested.load()) {
            result.error_code = Error::USER_CANCELLED; result.cancelled = true; finish(); return false;
        }
        result.instant_ai_readings.push_back({planned, actual, voltage, true, 0});
        result.progress.successful_reads.fetch_add(1);
        result.progress.completed_cycles.store(cycles.observe(voltage));
        if (timing.timed_out) {
            result.error_code = Error::INSTANT_AI_SCHEDULE_TIMEOUT; finish(); return false;
        }
    }
    finish();
    return dump_origin_data(config, result);
}
} // namespace Sampler
