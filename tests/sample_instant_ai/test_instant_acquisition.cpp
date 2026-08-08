#include "../../src/sampler/instant_acquisition.hpp"

#include <cassert>
#include <cmath>
#include <limits>

namespace {
class FakePlatform : public Sampler::InstantAi::InstantAcquisitionPlatform {
  public:
    Result::SamplingProgress* progress = nullptr;
    double now = 0.0;
    double start_offset = 0.01;
    double read_delay = 0.30;
    int error_code = 0;
    bool cancel_during_read = false;
    bool cancel_in_wait = false;
    int reads = 0;

    bool wait_until(double planned_seconds) override {
        now = planned_seconds + start_offset;
        if (cancel_in_wait) {
            progress->request_cancel();
            return false;
        }
        return true;
    }
    double now_seconds() override { return now; }
    int read(double& voltage) override {
        ++reads;
        const double phase = now - std::floor(now);
        voltage = phase < 0.5 ? 5.0 : 0.0;
        now += read_delay;
        if (cancel_during_read) progress->request_cancel();
        return error_code;
    }
    bool read_failed(int code) const override { return code != 0; }
};

Sampler::InstantAi::ContinuousSchedule dense_schedule() {
    Sampler::InstantAi::ContinuousSchedule schedule;
    schedule.duration_seconds = 4.0;
    schedule.polling_frequency_hz = 100.0;
    for (int i = 0; i <= 400; ++i) schedule.planned_seconds.push_back(i / 100.0);
    return schedule;
}
}

void test_instant_acquisition() {
    {
        Result::SamplingResult result;
        result.progress.reset(4.0, 4);
        FakePlatform platform;
        platform.progress = &result.progress;
        const bool success = Sampler::InstantAi::run_continuous_acquisition(
            dense_schedule(), 4, platform, result);
        assert(success);
        assert(std::fabs(result.instant_ai_readings[50].actual_seconds - 0.51) < 1e-12);
        assert(std::fabs(result.instant_ai_readings[50].completed_seconds - 0.81) < 1e-12);
        assert(result.progress.late_reads.load() == 0);
        const auto reconstruction = Sampler::InstantAi::reconstruct_continuous(
            result.instant_ai_readings, 3, 1.0, 100);
        assert(reconstruction.success);
        assert(reconstruction.late_reads == 0);
    }
    {
        Result::SamplingResult result;
        result.progress.reset(1.0, 1);
        FakePlatform platform;
        platform.progress = &result.progress;
        platform.error_code = static_cast<int>(Error::ErrorDeviceIoTimeOut);
        platform.cancel_during_read = true;
        Sampler::InstantAi::ContinuousSchedule schedule;
        schedule.duration_seconds = 1.0;
        schedule.planned_seconds.push_back(0.5);
        assert(!Sampler::InstantAi::run_continuous_acquisition(schedule, 1, platform, result));
        assert(result.error_code == Error::ErrorDeviceIoTimeOut);
        assert(!result.cancelled);
        assert(result.instant_ai_readings.size() == 1);
        assert(!result.instant_ai_readings[0].read_success);
        assert(std::isnan(result.instant_ai_readings[0].voltage));
    }
    {
        Result::SamplingResult result;
        result.progress.reset(1.0, 1);
        FakePlatform platform;
        platform.progress = &result.progress;
        platform.read_delay = 2.0;
        Sampler::InstantAi::ContinuousSchedule schedule;
        schedule.duration_seconds = 1.0;
        schedule.planned_seconds.push_back(0.5);
        assert(!Sampler::InstantAi::run_continuous_acquisition(schedule, 1, platform, result));
        assert(result.error_code == Error::INSTANT_AI_SCHEDULE_TIMEOUT);
        assert(result.instant_ai_readings[0].read_success);
        assert(result.instant_ai_readings[0].actual_seconds < 1.0);
        assert(result.instant_ai_readings[0].completed_seconds > 2.0);
    }
    {
        Result::SamplingResult result;
        result.progress.reset(1.0, 1);
        FakePlatform platform;
        platform.progress = &result.progress;
        platform.cancel_in_wait = true;
        Sampler::InstantAi::ContinuousSchedule schedule;
        schedule.duration_seconds = 1.0;
        schedule.planned_seconds.push_back(0.5);
        assert(!Sampler::InstantAi::run_continuous_acquisition(schedule, 1, platform, result));
        assert(result.error_code == Error::USER_CANCELLED);
        assert(result.cancelled);
        assert(platform.reads == 0);
    }
}
