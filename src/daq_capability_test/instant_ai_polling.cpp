#include "daq_capability_test/instant_ai_polling.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace daq_capability_test {
namespace {

CommandResult result(Status status, ExitCategory category, const std::string& code, const std::string& message) {
    CommandResult value;
    value.status = status;
    value.exit_category = category;
    value.code = code;
    value.message = message;
    return value;
}

double nearest_rank(std::vector<double> sorted, double percentile) {
    if (sorted.empty())
        return 0.0;
    std::sort(sorted.begin(), sorted.end());
    const size_t rank = static_cast<size_t>(std::ceil(percentile * sorted.size()));
    return sorted[(std::max)(size_t(1), rank) - 1];
}

bool finite_read(const InstantAiRead& read, size_t channel_count) {
    if (read.values.size() != channel_count || !std::isfinite(read.elapsed_seconds) || read.elapsed_seconds < 0.0 ||
        !std::isfinite(read.call_duration_us) || read.call_duration_us < 0.0 || !std::isfinite(read.interval_us) ||
        read.interval_us < 0.0) {
        return false;
    }
    for (size_t index = 0; index < read.values.size(); ++index) {
        if (!std::isfinite(read.values[index]))
            return false;
    }
    return true;
}

} // namespace

InstantAiStatistics instant_ai_statistics(const InstantAiPollingData& data) {
    InstantAiStatistics statistics;
    statistics.successful_reads = static_cast<unsigned long long>(data.reads.size());
    statistics.failed_reads = data.failed_reads;
    if (data.wall_duration_seconds > 0.0) {
        statistics.reads_per_second = data.reads.size() / data.wall_duration_seconds;
    }

    std::vector<double> intervals;
    for (size_t index = 1; index < data.reads.size(); ++index) {
        intervals.push_back(data.reads[index].interval_us);
    }
    if (!intervals.empty()) {
        statistics.mean_interval_us = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
        statistics.p95_interval_us = nearest_rank(intervals, 0.95);
        statistics.p99_interval_us = nearest_rank(intervals, 0.99);
        statistics.max_interval_us = *std::max_element(intervals.begin(), intervals.end());
    }

    if (!data.reads.empty()) {
        const size_t channel_count = data.reads[0].values.size();
        statistics.channel_min.assign(channel_count, 0.0);
        statistics.channel_max.assign(channel_count, 0.0);
        statistics.channel_span.assign(channel_count, 0.0);
        for (size_t channel = 0; channel < channel_count; ++channel) {
            statistics.channel_min[channel] = data.reads[0].values[channel];
            statistics.channel_max[channel] = data.reads[0].values[channel];
        }
        for (size_t read_index = 1; read_index < data.reads.size(); ++read_index) {
            if (data.reads[read_index].values.size() != channel_count)
                continue;
            for (size_t channel = 0; channel < channel_count; ++channel) {
                statistics.channel_min[channel] =
                    (std::min)(statistics.channel_min[channel], data.reads[read_index].values[channel]);
                statistics.channel_max[channel] =
                    (std::max)(statistics.channel_max[channel], data.reads[read_index].values[channel]);
            }
        }
        for (size_t channel = 0; channel < channel_count; ++channel) {
            statistics.channel_span[channel] = statistics.channel_max[channel] - statistics.channel_min[channel];
        }
    }
    return statistics;
}

CommandResult validate_instant_ai_polling(const InstantAiPollingRequest& request, const InstantAiPollingData& data,
                                          InstantAiStatistics& statistics) {
    statistics = instant_ai_statistics(data);
    if (data.reads.empty()) {
        return result(Status::Fail, ExitCategory::ValidationFailed, "INSTANT_AI_NO_SAMPLES",
                      "Instant AI returned no samples");
    }
    if (!std::isfinite(data.wall_duration_seconds) || data.wall_duration_seconds <= 0.0) {
        return result(Status::Fail, ExitCategory::ValidationFailed, "INVALID_INSTANT_AI_DATA",
                      "Polling duration is invalid");
    }
    const size_t channel_count = request.channels.size();
    if (data.channels != request.channels || channel_count == 0) {
        return result(Status::Fail, ExitCategory::ValidationFailed, "INVALID_INSTANT_AI_DATA",
                      "Returned channels do not match the request");
    }
    for (size_t index = 0; index < data.reads.size(); ++index) {
        if (!finite_read(data.reads[index], channel_count)) {
            CommandResult failed = result(Status::Fail, ExitCategory::ValidationFailed, "INVALID_INSTANT_AI_DATA",
                                          "Instant AI returned invalid sample data");
            failed.evidence["read_index"] = std::to_string(index);
            return failed;
        }
    }
    if (request.max_gap_provided && statistics.max_interval_us > request.max_gap_ms * 1000.0) {
        return result(Status::Fail, ExitCategory::ValidationFailed, "INSTANT_AI_GAP_EXCEEDED",
                      "Maximum Instant AI polling interval exceeded the configured threshold");
    }
    return result(Status::Pass, ExitCategory::Success, "INSTANT_AI_POLLING_STABLE",
                  "Instant AI polling completed without read failures");
}

} // namespace daq_capability_test
