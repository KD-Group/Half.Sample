#include "instant_acquisition.hpp"

#include "../constant.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Sampler {
namespace InstantAi {
namespace {
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

void finish(InstantAcquisitionPlatform& platform, Result::SamplingResult& result) {
    const double completed = platform.now_seconds();
    result.instant_ai_actual_duration_seconds = completed;
    result.progress.update_elapsed(completed);
}

bool cancel(InstantAcquisitionPlatform& platform, Result::SamplingResult& result) {
    result.error_code = Error::USER_CANCELLED;
    result.cancelled = true;
    finish(platform, result);
    return false;
}
} // namespace

bool run_continuous_acquisition(const ContinuousSchedule& schedule, int target_cycles,
                                InstantAcquisitionPlatform& platform, Result::SamplingResult& result) {
    result.instant_ai_waveforms.clear();
    result.instant_ai_readings.clear();
    result.instant_ai_format_version = 2;
    result.instant_ai_readings.reserve(schedule.planned_seconds.size());
    const double deadline = schedule.duration_seconds + std::max(1.0, schedule.duration_seconds * 0.1);
    CycleTracker cycles(target_cycles);
    double previous_planned = 0.0;
    for (double planned : schedule.planned_seconds) {
        if (result.progress.cancel_requested.load(std::memory_order_acquire) || !platform.wait_until(planned) ||
            result.progress.cancel_requested.load(std::memory_order_acquire))
            return cancel(platform, result);
        result.progress.update_elapsed(platform.now_seconds());
        const double started = platform.now_seconds();
        double voltage = std::numeric_limits<double>::quiet_NaN();
        const int code = platform.read(voltage);
        const double completed = platform.now_seconds();
        const ReadTiming timing = evaluate_read_timing(planned, started, completed, previous_planned, deadline);
        result.progress.update_elapsed(timing.completed_seconds);
        if (timing.late) {
            result.progress.late_reads.fetch_add(1);
            result.instant_ai_late_reads++;
        }
        previous_planned = planned;
        if (platform.read_failed(code)) {
            result.instant_ai_readings.push_back({planned, timing.actual_seconds,
                std::numeric_limits<double>::quiet_NaN(), false, code, timing.completed_seconds});
            result.error_code = static_cast<Error::Code>(code);
            finish(platform, result);
            return false;
        }
        result.instant_ai_readings.push_back(
            {planned, timing.actual_seconds, voltage, true, 0, timing.completed_seconds});
        result.progress.successful_reads.fetch_add(1);
        result.progress.completed_cycles.store(cycles.observe(voltage));
        if (result.progress.cancel_requested.load(std::memory_order_acquire))
            return cancel(platform, result);
        if (timing.timed_out) {
            result.error_code = Error::INSTANT_AI_SCHEDULE_TIMEOUT;
            finish(platform, result);
            return false;
        }
    }
    finish(platform, result);
    return true;
}

} // namespace InstantAi
} // namespace Sampler
