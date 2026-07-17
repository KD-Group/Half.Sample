#pragma once

#include "daq_capability_test/types.hpp"
#include "daq_capability_test/instant_ai_polling.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace daq_capability_test {

struct RawChannel {
    std::string name;
    std::vector<double> samples;
};
struct EnvironmentRecord {
    std::string executable_variant, build_id, os_architecture, process_architecture;
    std::string runtime_path, runtime_version, device_description, arguments;
};
struct CapabilityRecord {
    std::string device_description;
    unsigned int channel_count;
    double min_sample_rate_hz, max_sample_rate_hz;
    unsigned long long max_scan_count, buffer_capacity;
    bool trigger_supported;
    unsigned int trigger_count;
    std::string trigger_sources, trigger_actions;
    double trigger_delay_min, trigger_delay_max;
};
struct SummaryRecord {
    std::string test_name;
    unsigned int repetition;
    double requested_sample_rate_hz, actual_sample_rate_hz;
    unsigned int channel_count;
    unsigned long long requested_points_per_channel, actual_points_per_channel;
    double expected_duration_seconds, measured_duration_seconds;
    bool trigger_enabled;
    std::string trigger_source, trigger_edge, trigger_action;
    double trigger_delay, trigger_wait_seconds, wall_elapsed_seconds;
    bool timed_out, overrun, cache_overflow;
    std::string driver_error_code, driver_error_stage, status, code, note;
};

struct FsResult {
    bool success;
    bool already_exists;
    int error_code;
    std::string message;
    std::string stage;
    FsResult(bool ok = true, bool exists = false, int code = 0, const std::string& text = std::string(),
             const std::string& operation_stage = std::string())
        : success(ok), already_exists(exists), error_code(code), message(text), stage(operation_stage) {}
};

class FileOperations {
  public:
    virtual ~FileOperations() {}
    virtual FsResult create_directory(const std::string& path) = 0;
    virtual FsResult exists(const std::string& path) = 0;
    virtual FsResult write_exclusive(const std::string& path, const std::string& bytes) = 0;
    virtual FsResult publish_no_replace(const std::string& temporary, const std::string& final) = 0;
    virtual FsResult remove(const std::string& path) = 0;
};

namespace detail {
enum class PathFlavor { Windows, Posix };
struct DirectoryPlan {
    bool valid;
    std::string root_path;
    std::vector<std::string> directories;
    std::string error;
};
DirectoryPlan plan_directories(const std::string& path, PathFlavor flavor);
FsResult ensure_directory_path(FileOperations& operations, const std::string& path, PathFlavor flavor,
                               std::string& failed_path);
} // namespace detail

class ResultWriter {
  public:
    typedef std::function<std::string()> Clock;
    ResultWriter(const std::string& root, Clock clock,
                 std::shared_ptr<FileOperations> operations = std::shared_ptr<FileOperations>());
    CommandResult create_run_directory();
    const std::string& run_directory() const { return run_directory_; }
    CommandResult write_environment(const EnvironmentRecord& record);
    CommandResult write_capability(const CapabilityRecord& record);
    CommandResult write_summary(const std::vector<SummaryRecord>& records);
    CommandResult write_log(const std::vector<std::string>& lines);
    CommandResult write_raw(const std::string& test_name, unsigned int repetition,
                            const std::vector<RawChannel>& channels);
    CommandResult write_instant_ai_raw(const std::vector<int>& channels, const std::vector<InstantAiRead>& reads);
    CommandResult write_config_snapshot(const std::string& source, const std::string& filename);
    CommandResult write_completion_marker();

  private:
    CommandResult atomic_write(const std::string& target, const std::string& data);
    CommandResult failure(const std::string& path, const std::string& stage, const std::string& error,
                          const std::string& code = "OUTPUT_WRITE_FAILED") const;
    CommandResult exception_failure() const;
    bool initialized() const { return !run_directory_.empty(); }
    std::string root_, run_directory_;
    Clock clock_;
    std::shared_ptr<FileOperations> operations_;
};

} // namespace daq_capability_test
