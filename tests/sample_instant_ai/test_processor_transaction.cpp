#include "../../src/processor/processor.hpp"
#include "../../src/global/global.hpp"
#include "../../src/commander/measure.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

void test_processor_transaction() {
    Global::config.dump_file_path = "unchanged-config";
    Global::result.totalSamplingBuffer.assign(1, 123.0);
    Global::result.estimate.interval = 456.0;

    Config::SamplingConfig pending_config;
    pending_config.auto_mode = false;
    assert(pending_config.update(1, 0.05, 0.1, 100, 10.0));
    Result::SamplingResult pending_result(false);
    pending_result.resultWave.resize(50);
    for (std::size_t i = 0; i < pending_result.resultWave.size(); ++i)
        pending_result.resultWave[i] = 2.5 * std::exp(-static_cast<double>(i) / 10.0) + 5.0;

    assert(Commander::Processor::estimate(pending_config, pending_result));
    assert(pending_result.estimate.interval == pending_config.sampling_interval);
    assert(Global::config.dump_file_path == "unchanged-config");
    assert(Global::result.totalSamplingBuffer.size() == 1);
    assert(Global::result.totalSamplingBuffer[0] == 123.0);
    assert(Global::result.estimate.interval == 456.0);

    const char* const malformed_path = "cpp_build/transaction-malformed-v2.csv";
    {
        std::ofstream malformed(malformed_path);
        malformed << "#HALF_SAMPLE_INSTANT_AI_V2\nemitting_frequency=bad\n";
    }
    Global::result.success = true;
    std::istringstream command(std::string(malformed_path) + "\n");
    std::streambuf* original_input = std::cin.rdbuf(command.rdbuf());
    Commander::to_process();
    std::cin.rdbuf(original_input);
    assert(!Global::result.success);
    assert(Global::result.error_code == Error::INVALID_INSTANT_AI_CONFIG);
    assert(Global::config.dump_file_path == "unchanged-config");
    assert(Global::result.totalSamplingBuffer.size() == 1);
    assert(Global::result.totalSamplingBuffer[0] == 123.0);
    assert(Global::result.estimate.interval == 456.0);
    std::remove(malformed_path);
}
