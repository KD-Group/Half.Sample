#include "../../src/processor/independent_cycle.hpp"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <fstream>
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
