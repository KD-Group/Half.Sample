#include "../../src/result/sampling_result.hpp"

#include <cassert>

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
}
