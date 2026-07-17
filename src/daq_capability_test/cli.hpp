#pragma once

#include "daq_capability_test/suite_runner.hpp"

#include <string>
#include <vector>

namespace daq_capability_test {

enum class CliCommand {
    None,
    Help,
    Capability,
    Acquire,
    Trigger,
    InstantAiPolling,
    PhaseCapture,
    PhaseReconstruct,
    Suite
};

struct CliOptions {
    CliCommand command = CliCommand::None;
    std::string config_path, device, input_directory, output_directory;
    std::string invocation, executable_variant;
    std::vector<int> channels;
    std::string value_range, trigger_source, trigger_edge, trigger_action;
    double sample_rate_hz = 0.0, timeout_seconds = 0.0, max_edge_jitter_us = 0.0;
    double max_delay_position_error_us = 0.0;
    double target_phase_percent = 0.0;
    double duration_seconds = 30.0, poll_rate_hz = 0.0, max_gap_ms = 0.0;
    bool jitter_provided = false, delay_tolerance_provided = false, target_phase_provided = false;
    bool max_gap_provided = false;
    unsigned int points_per_channel = 0, repeat_count = 0;
    int trigger_delay_counts = 0;
    SuiteScope scope = SuiteScope::all();
};

struct CliParseResult {
    CliOptions options;
    CommandResult result;
    bool ok() const { return result.status == Status::Pass; }
};

CliParseResult parse_cli(int argc, char** argv);
int run_cli(DaqAdapter& adapter, int argc, char** argv);

} // namespace daq_capability_test
