#include "../../src/result/sampling_result.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>

void test_sampling_progress() {
    Result::SamplingProgress progress;
    progress.reset(40.0, 2);
    assert(progress.planned_milliseconds.load() == 40000);
    assert(progress.elapsed_milliseconds.load() == 0);
    assert(progress.completed_cycles.load() == 0);
    assert(progress.target_cycles.load() == 2);
    assert(progress.successful_reads.load() == 0);
    assert(progress.late_reads.load() == 0);
    assert(!progress.cancel_requested.load());
    progress.cancel_requested.store(true);
    assert(progress.cancel_requested.load());

    progress.reset(std::numeric_limits<double>::infinity(), 1);
    assert(progress.planned_milliseconds.load() == std::numeric_limits<long long>::max());
    progress.reset(-1.0, 1);
    assert(progress.planned_milliseconds.load() == 0);
    progress.reset(std::numeric_limits<double>::quiet_NaN(), 1);
    assert(progress.planned_milliseconds.load() == 0);
    progress.reset(1.0, -2);
    assert(progress.target_cycles.load() == 0);
    progress.elapsed_milliseconds.store(500);
    progress.update_elapsed(0.1);
    assert(progress.elapsed_milliseconds.load() == 500);
    progress.update_elapsed(0.75);
    assert(progress.elapsed_milliseconds.load() == 750);
    progress.request_cancel();
    assert(!progress.wait_until_or_cancel(std::chrono::steady_clock::now() + std::chrono::hours(1)));

    static_assert(std::is_same<decltype(Result::SamplingResult().measuring), std::atomic<bool>>::value,
                  "measuring must synchronize the command and worker threads");

    const Sampler::InstantAi::ReadTiming slow =
        Sampler::InstantAi::evaluate_read_timing(0.5, 0.51, 1.6, 0.4, 1.5);
    assert(slow.actual_seconds == 1.6);
    assert(!slow.late);
    assert(slow.timed_out);
    const Sampler::InstantAi::ReadTiming late =
        Sampler::InstantAi::evaluate_read_timing(0.5, 0.61, 0.62, 0.4, 1.5);
    assert(late.late);
    assert(!late.timed_out);
}
