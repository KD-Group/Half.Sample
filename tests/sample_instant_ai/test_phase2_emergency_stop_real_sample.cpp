#include "../../src/processor/independent_cycle.hpp"
#include "../../src/processor/processor.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>

namespace {

const char* const kPhase2EmergencyStop50HzAverage32Data =
    // Use the Windows short path so the C++ test can open the Chinese directory
    // without depending on the compiler source-file encoding.
    R"(D:\kunde\code\KDM3000\SAMPLE~1\13.txt)";

std::string regression_data_path() {
    const char* configured_path = std::getenv("HALF_SAMPLE_REGRESSION_DATA");
    return configured_path && *configured_path ? configured_path : kPhase2EmergencyStop50HzAverage32Data;
}

std::vector<double> load_samples(const std::string& path) {
    std::ifstream input(path);
    assert(input.good());

    std::vector<double> samples;
    samples.reserve(13200000);
    std::string value;
    while (std::getline(input, value, ',')) {
        if (!value.empty()) {
            samples.push_back(std::stod(value));
        }
    }
    return samples;
}

void assert_real_sample_result(const std::vector<double>& samples, int expected_candidates, int requested_waveforms) {
    const auto result = Commander::Processor::extract_independent_cycles(
        samples, 20000000, 50.0, requested_waveforms);
    assert(result.success);
    assert(result.candidate_waveforms == expected_candidates);
    assert(result.discarded_waveforms == 1);
    assert(result.accepted_waveforms == requested_waveforms);
    assert(result.cycles.size() == static_cast<std::size_t>(requested_waveforms));

    Config::SamplingConfig config;
    config.auto_mode = false;
    assert(config.update(requested_waveforms, 50.0, 0.0, 100, 10.0, "independent_cycle"));
    Result::SamplingResult processed(false);
    processed.totalSamplingBuffer = samples;
    processed.resultWave.assign(static_cast<std::size_t>(config.valid_length), 0.0);
    assert(Commander::Processor::align(config, processed));
    assert(Commander::Processor::summation(config, processed));
    assert(processed.complete_waveforms == requested_waveforms);
    assert(processed.resultWave.size() == static_cast<std::size_t>(config.valid_length));
    const auto limits = std::minmax_element(processed.resultWave.begin(), processed.resultWave.end());
    assert(*limits.second - *limits.first > 0.05);
    assert(Commander::Processor::estimate(config, processed));
    assert(processed.estimate.y);
    const auto fitted_limits = std::minmax_element(processed.estimate.y->begin(), processed.estimate.y->end());
    assert(*fitted_limits.second - *fitted_limits.first > 0.02);
}

} // namespace

void test_phase2_emergency_stop_50hz_average32_supports_32_cycles() {
    const std::string path = regression_data_path();
    std::ifstream probe(path);
    if (!probe.good()) {
        std::cout << "SKIP phase2_emergency_stop_50hz_average32: data not found: " << path << '\n';
        return;
    }

    const std::vector<double> samples = load_samples(path);
    assert(samples.size() == 13200000);
    assert_real_sample_result(samples, 33, 32);
}

void test_phase2_emergency_stop_50hz_average32_doubled_supports_64_cycles() {
    const std::string path = regression_data_path();
    std::ifstream probe(path);
    if (!probe.good()) {
        std::cout << "SKIP phase2_emergency_stop_50hz_average32_doubled: data not found: " << path << '\n';
        return;
    }

    const std::vector<double> samples = load_samples(path);
    assert(samples.size() == 13200000);
    std::vector<double> doubled = samples;
    doubled.insert(doubled.end(), samples.begin(), samples.end());
    assert(doubled.size() == 26400000);
    assert_real_sample_result(doubled, 66, 64);
}
