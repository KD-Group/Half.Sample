#include "../../src/sampler/mock_sampler.hpp"

#include <cassert>
#include <limits>

#include "../../src/config/sampling_config.hpp"

void test_mock_sampler_controls() {
    Sampler::MockSampler mock;
    assert(mock.set_value("mock_phase_offset", 0.25));
    assert(!mock.set_value("mock_phase_offset", std::numeric_limits<double>::quiet_NaN()));
    assert(!mock.set_value("mock_phase_offset", std::numeric_limits<double>::infinity()));
    assert(mock.get_value("mock_phase_offset") == 0.25);

    Config::SamplingConfig config;
    assert(config.update(1, 0.05, 0.1, 100, 10.0));
    Result::SamplingResult cancelled;
    cancelled.progress.reset(config.instant_ai_planned_duration_seconds, 2);
    cancelled.progress.cancel_requested.store(true);
    assert(!mock.sample(config, cancelled));
    assert(cancelled.error_code == Error::USER_CANCELLED);
    assert(cancelled.cancelled);

    Result::SamplingResult completed;
    completed.progress.reset(config.instant_ai_planned_duration_seconds, 2);
    assert(mock.sample(config, completed));
    assert(completed.progress.successful_reads.load() ==
           static_cast<int>(completed.instant_ai_readings.size()));
    assert(completed.progress.elapsed_milliseconds.load() ==
           completed.progress.planned_milliseconds.load());
    assert(completed.progress.completed_cycles.load() <= completed.progress.target_cycles.load());

    Config::SamplingConfig buffered_config;
    assert(buffered_config.update(2, 100.0, 0.0, 100, 10.0,
                                  "independent_cycle"));
    buffered_config.waveform_length = 100;
    buffered_config.sampling_length_per_sample = 300;
    buffered_config.sampling_time = 2;
    Result::SamplingResult buffered(false);
    buffered.totalSamplingBuffer.assign(600, 0.0);
    assert(mock.set_value("mock_v0", 0.0));
    assert(mock.set_value("mock_v_inf", 0.0));
    assert(mock.set_value("mock_noise", 0.0));

    assert(mock.sample(buffered_config, buffered));

    assert(buffered.totalSamplingBuffer[0] ==
           buffered.totalSamplingBuffer[300]);
}
