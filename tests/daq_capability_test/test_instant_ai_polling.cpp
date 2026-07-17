#include "daq_capability_test/instant_ai_polling.hpp"
#include "daq_capability_test/cli.hpp"
#include "daq_capability_test/fake_daq_adapter.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

using namespace daq_capability_test;

namespace {

InstantAiRead read(unsigned long long index, double elapsed, double call_us, double interval_us,
                   std::initializer_list<double> values) {
    InstantAiRead value;
    value.read_index = index;
    value.elapsed_seconds = elapsed;
    value.call_duration_us = call_us;
    value.interval_us = interval_us;
    value.values.assign(values.begin(), values.end());
    return value;
}

void test_statistics_ignore_the_first_zero_interval() {
    InstantAiPollingData one;
    one.channels.push_back(0);
    one.reads.push_back(read(0, 0.001, 80.0, 0.0, {1.0}));
    one.wall_duration_seconds = 0.001;
    const InstantAiStatistics single = instant_ai_statistics(one);
    assert(single.successful_reads == 1);
    assert(single.mean_interval_us == 0.0);
    assert(single.p95_interval_us == 0.0);
    assert(single.p99_interval_us == 0.0);
    assert(single.max_interval_us == 0.0);

    InstantAiPollingData many;
    many.channels.push_back(0);
    many.reads.push_back(read(0, 0.0001, 50.0, 0.0, {1.0}));
    many.reads.push_back(read(1, 0.0002, 40.0, 100.0, {2.0}));
    many.reads.push_back(read(2, 0.0005, 45.0, 300.0, {3.0}));
    many.wall_duration_seconds = 0.0005;
    const InstantAiStatistics stats = instant_ai_statistics(many);
    assert(stats.successful_reads == 3);
    assert(std::fabs(stats.reads_per_second - 6000.0) < 1e-9);
    assert(stats.mean_interval_us == 200.0);
    assert(stats.p95_interval_us == 300.0);
    assert(stats.p99_interval_us == 300.0);
    assert(stats.max_interval_us == 300.0);
    assert((stats.channel_min == std::vector<double>{1.0}));
    assert((stats.channel_max == std::vector<double>{3.0}));
    assert((stats.channel_span == std::vector<double>{2.0}));
}

void test_polling_validation_reports_dedicated_failures() {
    InstantAiPollingRequest request;
    request.device = "Demo";
    request.channels.push_back(0);
    request.value_range = "-10V~10V";
    request.duration_seconds = 1.0;
    InstantAiStatistics stats;
    InstantAiPollingData empty;
    assert(validate_instant_ai_polling(request, empty, stats).code == "INSTANT_AI_NO_SAMPLES");

    InstantAiPollingData data;
    data.channels.push_back(0);
    data.reads.push_back(read(0, 0.0001, 50.0, 0.0, {1.0}));
    data.reads.push_back(read(1, 0.0004, 40.0, 300.0, {2.0}));
    data.wall_duration_seconds = 0.0004;
    request.max_gap_provided = true;
    request.max_gap_ms = 0.2;
    assert(validate_instant_ai_polling(request, data, stats).code == "INSTANT_AI_GAP_EXCEEDED");

    request.max_gap_provided = false;
    data.reads[1].values[0] = std::numeric_limits<double>::quiet_NaN();
    assert(validate_instant_ai_polling(request, data, stats).code == "INVALID_INSTANT_AI_DATA");
}

CliParseResult parse(std::initializer_list<const char*> arguments) {
    std::vector<std::string> storage;
    for (const char* argument : arguments)
        storage.push_back(argument);
    std::vector<char*> argv;
    for (size_t index = 0; index < storage.size(); ++index) {
        argv.push_back(&storage[index][0]);
    }
    return parse_cli(static_cast<int>(argv.size()), argv.data());
}

void test_instant_ai_cli_defaults_and_validation() {
    const CliParseResult valid =
        parse({"legacy.exe", "instant-ai-polling", "--channels", "0", "--range", "-10V~10V", "--output-dir", "out"});
    assert(valid.ok());
    assert(valid.options.command == CliCommand::InstantAiPolling);
    assert(valid.options.duration_seconds == 30.0);
    assert(valid.options.poll_rate_hz == 0.0);
    assert(!valid.options.max_gap_provided);

    const CliParseResult explicit_values =
        parse({"legacy.exe", "instant-ai-polling", "--channels", "0,1", "--range", "-10V~10V", "--duration", "2.5",
               "--poll-rate", "10", "--max-gap-ms", "125", "--output-dir", "out"});
    assert(explicit_values.ok());
    assert(explicit_values.options.duration_seconds == 2.5);
    assert(explicit_values.options.poll_rate_hz == 10.0);
    assert(explicit_values.options.max_gap_provided);
    assert(explicit_values.options.max_gap_ms == 125.0);

    assert(!parse({"legacy.exe", "instant-ai-polling", "--channels", "0", "--range", "-10V~10V", "--duration", "0",
                   "--output-dir", "out"})
                .ok());
    assert(!parse({"legacy.exe", "instant-ai-polling", "--channels", "0", "--range", "-10V~10V", "--max-gap-ms", "0",
                   "--output-dir", "out"})
                .ok());
    assert(!parse({"legacy.exe", "instant-ai-polling", "--channels", "0", "--range", "-10V~10V", "--poll-rate", "0",
                   "--output-dir", "out"})
                .ok());
    const CliParseResult non_contiguous =
        parse({"legacy.exe", "instant-ai-polling", "--channels", "0,2", "--range", "-10V~10V", "--output-dir", "out"});
    assert(!non_contiguous.ok());
    assert(non_contiguous.result.code == "NON_CONTIGUOUS_CHANNELS");
}

class ScriptedInstantAiAdapter : public FakeDaqAdapter {
  public:
    AdapterResult<InstantAiPollingData> poll_instant_ai(const InstantAiPollingRequest& request) override {
        AdapterResult<InstantAiPollingData> result;
        result.success = true;
        result.value.channels = request.channels;
        result.value.runtime_path = "C:/biodaq.dll";
        result.value.runtime_version = "1.2.3.4";
        result.value.reads.push_back(read(0, 0.0001, 50.0, 0.0, {1.0}));
        result.value.reads.push_back(read(1, 0.0002, 40.0, 100.0, {2.0}));
        result.value.reads.push_back(read(2, 0.0005, 45.0, 300.0, {3.0}));
        result.value.wall_duration_seconds = 0.0005;
        return result;
    }
};

std::pair<int, std::string> run_scripted_cli(ScriptedInstantAiAdapter& adapter,
                                             std::initializer_list<const char*> arguments) {
    std::vector<std::string> storage;
    for (const char* argument : arguments)
        storage.push_back(argument);
    std::vector<char*> argv;
    for (size_t index = 0; index < storage.size(); ++index)
        argv.push_back(&storage[index][0]);
    std::ostringstream stdout_capture, stderr_capture;
    std::streambuf* old_stdout = std::cout.rdbuf(stdout_capture.rdbuf());
    std::streambuf* old_stderr = std::cerr.rdbuf(stderr_capture.rdbuf());
    const int exit = run_cli(adapter, static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(old_stdout);
    std::cerr.rdbuf(old_stderr);
    return std::make_pair(exit, stdout_capture.str());
}

void test_instant_ai_cli_emits_statistics_and_applies_gap_threshold() {
    ScriptedInstantAiAdapter adapter;
    const std::pair<int, std::string> passed =
        run_scripted_cli(adapter, {"legacy.exe", "instant-ai-polling", "--channels", "0", "--range", "-10V~10V",
                                   "--duration", "0.0004", "--output-dir", "cpp_build/instant_cli_test"});
    assert(passed.first == 0);
    assert(passed.second.find("\"code\":\"INSTANT_AI_POLLING_STABLE\"") != std::string::npos);
    assert(passed.second.find("\"successful_reads\":\"3\"") != std::string::npos);
    assert(passed.second.find("\"max_interval_us\":\"300") != std::string::npos);

    const std::pair<int, std::string> failed = run_scripted_cli(
        adapter, {"legacy.exe", "instant-ai-polling", "--channels", "0", "--range", "-10V~10V", "--duration", "0.0004",
                  "--max-gap-ms", "0.2", "--output-dir", "cpp_build/instant_cli_test"});
    assert(failed.first == 2);
    assert(failed.second.find("\"code\":\"INSTANT_AI_GAP_EXCEEDED\"") != std::string::npos);
}

} // namespace

void test_instant_ai_polling() {
    test_statistics_ignore_the_first_zero_interval();
    test_polling_validation_reports_dedicated_failures();
    test_instant_ai_cli_defaults_and_validation();
    test_instant_ai_cli_emits_statistics_and_applies_gap_threshold();
}
