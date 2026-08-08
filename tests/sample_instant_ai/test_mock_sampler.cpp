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
}
