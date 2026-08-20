#include "../../src/processor/independent_cycle.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "../../src/config/sampling_config.hpp"
#include "../../src/processor/processor.hpp"
#include "../../src/result/sampling_result.hpp"

namespace {
constexpr int SamplingFrequency = 20'000'000;
constexpr double EmittingFrequency = 50.0;
constexpr int Period = 400'000;

std::vector<double> make_samples(const std::vector<int>& edges) {
    const int length = edges.back() + 200'000;
    std::vector<double> samples(static_cast<std::size_t>(length), -0.55);
    for (const int edge : edges) {
        for (int point = edge; point < edge + 100'000 && point < length; ++point)
            samples[static_cast<std::size_t>(point)] = -0.35;
    }
    return samples;
}

std::vector<double> make_shifted_batch(double low, double high) {
    std::vector<double> samples(2'500, low);
    const int rising_edges[] = {500, 1'500};
    for (const int rising_edge : rising_edges) {
        for (int point = rising_edge; point < rising_edge + 400; ++point)
            samples[static_cast<std::size_t>(point)] = high;
    }
    return samples;
}

std::vector<double> load_samples(const std::string& path) {
    std::ifstream input(path);
    assert(input.good());
    std::vector<double> samples;
    std::string value;
    while (std::getline(input, value, ',')) {
        if (!value.empty()) samples.push_back(std::stod(value));
    }
    return samples;
}

std::string regression_directory() {
    const char* configured = std::getenv("HALF_SAMPLE_REGRESSION_DIR");
    return configured && *configured ? configured : "";
}
} // namespace

void test_independent_cycle_rejects_short_threshold_candidate() {
    const auto samples = make_samples({300, 400'300, 600'300, 800'300});
    const auto result = Commander::Processor::extract_independent_cycles(
        samples, SamplingFrequency, EmittingFrequency, 2);
    assert(!result.success);
    assert(result.accepted_waveforms < 2);
    assert(result.rejected_short_periods > 0 || result.rejected_threshold_candidates > 0);
}

void test_align_keeps_voltage_when_amplitude_is_below_threshold() {
    Config::SamplingConfig config;
    config.waveform_processing_mode = "independent_cycle";
    Result::SamplingResult result(false);
    result.totalSamplingBuffer = {-0.498046875, -0.402832031};

    assert(!Commander::Processor::align(config, result));
    assert(result.error_code == Error::VOLTAGE_NOT_ENOUGH);
    assert(std::fabs(result.v_inf - 0.095214844) < 1e-12);
    assert(result.v_inf_valid);
}

void test_align_rejects_non_finite_voltage_as_unreported() {
    Config::SamplingConfig config;
    Result::SamplingResult result(false);
    result.totalSamplingBuffer = {0.0, std::numeric_limits<double>::infinity()};

    assert(!Commander::Processor::align(config, result));
    assert(!result.v_inf_valid);
    assert(result.v_inf == 0.0);
}

void test_independent_cycle_rejects_long_period() {
    const auto samples = make_samples({300, 400'300, 960'300, 1'360'300});
    const auto result = Commander::Processor::extract_independent_cycles(
        samples, SamplingFrequency, EmittingFrequency, 2);

    assert(!result.success || result.accepted_waveforms < 2);
    assert(result.rejected_long_periods > 0);
}

void test_independent_cycle_normal_samples_have_identifiable_tau() {
    const std::string directory = regression_directory();
    if (directory.empty()) return;

    struct Case {
        const char* name;
        int waveforms;
    };
    const Case cases[] = {{"20.txt", 16}, {"21.txt", 32}, {"24.txt", 1},
                          {"26.txt", 1}, {"27.txt", 1}};
    for (const auto& test_case : cases) {
        const std::vector<double> samples = load_samples(directory + "\\" + test_case.name);
        Config::SamplingConfig config;
        config.auto_mode = false;
        assert(config.update(test_case.waveforms, 50.0, 0.0, 100, 10.0, "independent_cycle"));
        Result::SamplingResult result(false);
        result.totalSamplingBuffer = samples;
        result.resultWave.assign(static_cast<std::size_t>(config.valid_length), 0.0);
        assert(Commander::Processor::align(config, result));
        assert(Commander::Processor::summation(config, result));
        assert(Commander::Processor::estimate(config, result));
        assert(result.estimate.tau > 50.0);
        assert(result.estimate.tau < 10'000.0);
    }
}

void test_independent_cycle_accumulates_valid_cycles_across_batches() {
    const std::string directory = regression_directory();
    if (directory.empty()) return;

    const std::vector<double> samples = load_samples(directory + "\\20.txt");
    Config::SamplingConfig config;
    config.auto_mode = false;
    assert(config.update(32, 50.0, 0.0, 100, 10.0, "independent_cycle"));
    Result::SamplingResult result(false);
    result.totalSamplingBuffer = samples;
    result.resultWave.assign(static_cast<std::size_t>(config.valid_length), 0.0);
    assert(Commander::Processor::align(config, result));
    assert(!Commander::Processor::summation(config, result));
    assert(result.complete_waveforms == 16);
    assert(result.independent_batches == 1);
    assert(Commander::Processor::summation(config, result));
    assert(result.complete_waveforms == 32);
    assert(result.independent_batches == 2);
}

void test_independent_cycle_uses_edge_platform_means_for_voltage() {
    constexpr int sampling_frequency = 1'000;
    constexpr double emitting_frequency = 1.0;
    std::vector<double> samples(3'000, 1.0);

    const int rising_edges[] = {500, 1'500, 2'500};
    for (const int rising_edge : rising_edges) {
        const int falling_edge = rising_edge + 400;
        for (int point = rising_edge; point < falling_edge; ++point)
            samples[static_cast<std::size_t>(point)] = 3.0;

        // The edge-adjacent platforms deliberately differ from the cycle extrema.
        // With a 5% platform window and a 1% guard, these are the values that
        // must define the cycle voltage instead of P99/P01.
        for (int point = falling_edge - 100; point < falling_edge; ++point)
            samples[static_cast<std::size_t>(point)] = 2.5;
        for (int point = rising_edge - 100; point < rising_edge; ++point)
            samples[static_cast<std::size_t>(point)] = 1.2;
    }

    const auto result = Commander::Processor::extract_independent_cycles(
        samples, sampling_frequency, emitting_frequency, 2);

    assert(result.success);
    assert(result.cycle_maximums.size() == 2);
    assert(result.cycle_minimums.size() == 2);
    assert(std::fabs(result.cycle_maximums[0] - 2.5) < 1e-9);
    assert(std::fabs(result.cycle_minimums[0] - 1.2) < 1e-9);
    assert(std::fabs(result.voltage_amplitudes[0] - 1.3) < 1e-9);
}

void test_independent_cycle_guard_is_one_percent_for_short_periods() {
    constexpr int waveform_length = 500;
    std::vector<double> samples(1'900, 1.0);
    for (const int rising_edge : {350, 850, 1'350}) {
        const int falling_edge = rising_edge + 200;
        for (int point = rising_edge; point < falling_edge; ++point)
            samples[static_cast<std::size_t>(point)] = 3.0;

        // Confirmation records this falling boundary 16 points early.  The
        // correct 5-point guard therefore averages [edge-30, edge-5).
        const int confirmed_edge = falling_edge - 16;
        for (int point = confirmed_edge - 30;
             point < confirmed_edge - 8; ++point)
            samples[static_cast<std::size_t>(point)] = 2.8;
        for (int point = confirmed_edge - 8;
             point < confirmed_edge - 5; ++point)
            samples[static_cast<std::size_t>(point)] = 2.6;
    }

    const auto result = Commander::Processor::extract_independent_cycles(
        samples, 1'000, 2.0, 2);

    assert(result.success);
    assert(std::fabs(result.cycle_maximums[0] - 2.776) < 1e-9);
    assert(std::fabs(result.cycle_minimums[0] - 1.0) < 1e-9);
}

void test_independent_cycle_summation_keeps_acquisition_batches_independent() {
    Config::SamplingConfig config;
    config.sampling_frequency = 1'000;
    config.emitting_frequency = 1.0;
    config.sampling_interval = 1'000.0;
    config.sampling_length_per_sample = 2'500;
    config.sampling_time = 2;
    config.waveform_length = 1'000;
    config.valid_length = 1'000;
    config.number_of_waveforms = 2;
    config.auto_mode = false;
    config.waveform_processing_mode = "independent_cycle";

    const auto first_batch = make_shifted_batch(0.0, 2.0);
    const auto second_batch = make_shifted_batch(10.0, 14.0);
    Result::SamplingResult result(false);
    result.totalSamplingBuffer = first_batch;
    result.totalSamplingBuffer.insert(result.totalSamplingBuffer.end(),
                                      second_batch.begin(), second_batch.end());
    result.resultWave.assign(static_cast<std::size_t>(config.valid_length), 0.0);

    assert(Commander::Processor::summation(config, result));
    assert(result.complete_waveforms == 2);
    assert(result.independent_batches == 2);
    assert(std::fabs(result.cycle_maximum - 8.0) < 1e-9);
    assert(std::fabs(result.cycle_minimum - 5.0) < 1e-9);
    assert(std::fabs(result.v_inf - 3.0) < 1e-9);
    assert(result.v_inf_valid);
}

void test_independent_cycle_platform_windows_do_not_cross_batch_start() {
    constexpr std::size_t batch_size = 2'500;
    std::vector<double> samples(batch_size * 2, 1.0);
    const std::size_t begin = batch_size;
    for (const int rising_edge : {50, 1'050, 2'050}) {
        for (int point = rising_edge; point < rising_edge + 400; ++point) {
            samples[begin + static_cast<std::size_t>(point)] = 3.0;
        }
    }

    const auto result = Commander::Processor::extract_independent_cycles(
        samples, begin, samples.size(), 1'000, 1.0, 2);

    assert(!result.success);
    assert(result.accepted_waveforms == 1);
}
