#pragma once

#include <string>
#include <vector>
#include <algorithm>

namespace daq_capability_test {

template <typename T>
struct AdapterResult {
    bool success = false;
    bool unsupported = false;
    std::string code;
    std::string message;
    std::string stage;
    std::string driver_error;
    std::vector<std::string> evidence;
    T value{};
};

struct OperationInfo {
    int function_table_version = 0;
    int function_table_revision = 0;
    int channel_start = 0;
    unsigned int channel_count = 0;
    unsigned int points_per_channel = 0;
    double sample_rate_hz = 0.0;
    std::string value_range;
    int actual_channel_start = 0;
    unsigned int actual_channel_count = 0;
    unsigned int actual_samples = 0;
    std::vector<std::string> actual_ranges;
};

struct CapabilityInfo {
    bool supports_acquisition = true;
    bool supports_trigger = false;
    unsigned int max_channels = 1;
    double min_sample_rate_hz = 0.0;
    double max_sample_rate_hz = 0.0;
    unsigned int max_points_per_channel = 0;
    unsigned int buffer_capacity = 0;
    std::vector<std::string> supported_ranges;
    unsigned int max_scan_count = 0;
    unsigned int trigger_count = 0;
    std::vector<std::string> trigger_sources;
    std::vector<std::string> trigger_actions;
    double trigger_delay_min = 0.0;
    double trigger_delay_max = 0.0;
    std::string runtime_path;
    std::string runtime_version;
    int function_table_version = 0;
    int function_table_revision = 0;
    std::vector<std::string> evidence;
};

struct AcquisitionRequest {
    std::string device;
    std::vector<int> channels;
    std::string value_range;
    double sample_rate_hz = 0.0;
    unsigned int points_per_channel = 0;
    double timeout_seconds = 0.0;
    std::string role = "ordinary";
    std::string mock_scenario;
    double mock_signal_frequency_hz = 1000.0;
};

struct ChannelData { std::vector<double> samples; };

struct AcquisitionData {
    std::vector<ChannelData> channels;
    double actual_sample_rate_hz = 0.0;
    double duration_seconds = 0.0;
    double trigger_wait_seconds = 0.0;
    double wall_elapsed_seconds = 0.0;
    bool timed_out = false;
    bool timeout_observable = true;
    bool overrun = false;
    bool cache_overflow = false;
    std::string layout;
    bool layout_unverified = false;
    bool layout_requires_hardware_confirmation = false;
    std::vector<std::string> evidence;
};

inline void map_acquisition_timing(AcquisitionData& data,unsigned int returned_points,double actual_rate,double wall_elapsed)
{data.duration_seconds=returned_points/actual_rate;data.wall_elapsed_seconds=wall_elapsed;data.trigger_wait_seconds=(std::max)(0.0,wall_elapsed-data.duration_seconds);}

struct TriggerRequest {
    std::string source;
    std::string edge;
    std::string action;
    int delay_counts = 0;
    std::string mock_scenario;
};

class DaqAdapter {
public:
    virtual ~DaqAdapter() {}
    virtual AdapterResult<CapabilityInfo> query_capabilities(const std::string& device) = 0;
    virtual AdapterResult<OperationInfo> configure(const AcquisitionRequest& request) = 0;
    virtual AdapterResult<AcquisitionData> acquire_once(const AcquisitionRequest& request) = 0;
    virtual AdapterResult<OperationInfo> configure_trigger(const TriggerRequest& request) = 0;
    virtual void stop() noexcept = 0;
};

}  // namespace daq_capability_test
