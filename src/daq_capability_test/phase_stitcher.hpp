#pragma once

#include "daq_capability_test/types.hpp"

#include <map>
#include <string>
#include <vector>

namespace daq_capability_test {

enum class EdgeDirection { Rising, Falling };

struct Segment {
    int id = 0;
    int acquisition_order = 0;
    double sample_rate_hz = 0.0;
    std::vector<double> timestamps;
    std::vector<double> sample_channel;
    std::vector<double> reference_channel;
    bool has_known_phase = false;
    double known_phase = 0.0;
    double trigger_delay_seconds = 0.0;
    bool reference_calibration = false;
};

struct StitchConfig {
    double signal_frequency_hz = 0.0;
    int phase_bin_count = 0;
    int min_samples_per_bin = 1;
    int requested_waveforms = 1;
    int max_attempts = 1;
    double reference_threshold_v = 0.0;
    double reference_hysteresis_v = 0.0;
    EdgeDirection edge_direction = EdgeDirection::Rising;
    double min_reference_span_v = 0.0;
    double max_frequency_error_percent = 0.0;
    double max_edge_jitter_us = 0.0;
    double reference_duty_cycle_percent = 50.0;
    double max_duty_cycle_error_percent = 0.0;
    double min_overlap_percent = 0.0;
    double max_overlap_error_v = 0.0;
    double max_response_drift_v = 0.0;
    double max_boundary_jump_v = 0.0;
};

struct ReconstructedWaveform {
    std::vector<double> phase_bins;
    std::vector<double> values;
    std::vector<int> segment_ids;
    double coverage_percent = 0.0;
    std::map<std::string, std::string> evidence;
};

struct StitchResult {
    CommandResult command;
    std::vector<ReconstructedWaveform> waveforms;
};

StitchResult stitch_phase_waveforms(const std::vector<Segment>& segments, const StitchConfig& config);

} // namespace daq_capability_test
