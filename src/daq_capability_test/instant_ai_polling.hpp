#pragma once

#include "daq_capability_test/types.hpp"

#include <string>
#include <vector>

namespace daq_capability_test {

struct InstantAiPollingRequest {
    std::string device;
    std::vector<int> channels;
    std::string value_range;
    double duration_seconds = 30.0;
    double poll_rate_hz = 0.0;
    bool max_gap_provided = false;
    double max_gap_ms = 0.0;
};

struct InstantAiRead {
    unsigned long long read_index = 0;
    double elapsed_seconds = 0.0;
    double call_duration_us = 0.0;
    double interval_us = 0.0;
    std::vector<double> values;
};

struct InstantAiPollingData {
    std::vector<int> channels;
    std::vector<InstantAiRead> reads;
    double wall_duration_seconds = 0.0;
    unsigned long long failed_reads = 0;
    std::string runtime_path;
    std::string runtime_version;
};

struct InstantAiStatistics {
    unsigned long long successful_reads = 0;
    unsigned long long failed_reads = 0;
    double reads_per_second = 0.0;
    double mean_interval_us = 0.0;
    double p95_interval_us = 0.0;
    double p99_interval_us = 0.0;
    double max_interval_us = 0.0;
    std::vector<double> channel_min;
    std::vector<double> channel_max;
    std::vector<double> channel_span;
};

InstantAiStatistics instant_ai_statistics(const InstantAiPollingData& data);
CommandResult validate_instant_ai_polling(const InstantAiPollingRequest& request, const InstantAiPollingData& data,
                                          InstantAiStatistics& statistics);

} // namespace daq_capability_test
