#include "../../src/result/sampling_result.hpp"
#include "../../src/commander/base.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <sstream>
#include <type_traits>

void test_sampling_progress() {
    std::ostringstream output;
    std::streambuf* original_output = std::cout.rdbuf(output.rdbuf());
    Commander::Base::line("message", std::string("plain \"quote\" \\ slash\rline\nnext\ttab"));
    std::cout.rdbuf(original_output);
    assert(output.str() == "message = \"plain \\\"quote\\\" \\\\ slash\\rline\\nnext\\ttab\"\n");

    Result::SamplingProgress progress;
    assert(Result::SamplingProgress::to_milliseconds(std::numeric_limits<double>::quiet_NaN()) == 0);
    assert(Result::SamplingProgress::to_milliseconds(-1.0) == 0);
    assert(Result::SamplingProgress::to_milliseconds(std::numeric_limits<double>::infinity()) ==
           std::numeric_limits<long long>::max());
    assert(Result::SamplingProgress::to_milliseconds(std::numeric_limits<double>::max()) ==
           std::numeric_limits<long long>::max());
    assert(Result::SamplingProgress::to_milliseconds(1.25) == 1250);
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

    Result::SamplingResult deferred(false);
    assert(deferred.totalSamplingBuffer.empty());
    assert(deferred.resultWave.empty());

    const Sampler::InstantAi::ReadTiming slow =
        Sampler::InstantAi::evaluate_read_timing(0.5, 0.51, 1.6, 0.4, 1.5);
    assert(slow.actual_seconds == 0.51);
    assert(slow.completed_seconds == 1.6);
    assert(!slow.late);
    assert(slow.timed_out);
    const Sampler::InstantAi::ReadTiming late =
        Sampler::InstantAi::evaluate_read_timing(0.5, 0.61, 0.62, 0.4, 1.5);
    assert(late.late);
    assert(!late.timed_out);
}
