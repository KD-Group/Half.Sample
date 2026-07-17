#include "daq_capability_test/phase_stitcher.hpp"

#include <cassert>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <set>

using namespace daq_capability_test;

namespace {
const double pi = 3.14159265358979323846;

StitchConfig config() {
    StitchConfig c;
    c.signal_frequency_hz = 10.0;
    c.phase_bin_count = 10;
    c.min_samples_per_bin = 1;
    c.requested_waveforms = 1;
    c.max_attempts = 100;
    c.reference_threshold_v = 0.5;
    c.reference_hysteresis_v = 0.1;
    c.edge_direction = EdgeDirection::Rising;
    c.min_reference_span_v = 0.8;
    c.max_frequency_error_percent = 5.0;
    c.max_edge_jitter_us = 2000.0;
    c.reference_duty_cycle_percent = 50.0;
    c.max_duty_cycle_error_percent = 2.0;
    c.min_overlap_percent = 10.0;
    c.max_overlap_error_v = 0.2;
    c.max_response_drift_v = 0.2;
    c.max_boundary_jump_v = 1.0;
    return c;
}

Segment known(int id, int order, double start_phase, double offset = 0.0, double scale = 1.0) {
    Segment s;
    s.id = id;
    s.acquisition_order = order;
    s.sample_rate_hz = 100.0;
    s.has_known_phase = true;
    s.known_phase = start_phase;
    for (int i = 0; i < 10; ++i)
        s.sample_channel.push_back(offset + scale * std::sin(2.0 * pi * (start_phase + i / 10.0)));
    return s;
}

Segment referenced(int id, EdgeDirection direction, double period = 0.1, double jitter = 0.0) {
    Segment s;
    s.id = id;
    s.acquisition_order = id;
    s.sample_rate_hz = 1000.0;
    for (int i = 0; i < 310; ++i) {
        const double t = i / 1000.0;
        const double shifted = t + (t >= 0.2 ? jitter : 0.0);
        double phase = std::fmod(shifted, period) / period;
        bool high = phase >= 0.5;
        if (direction == EdgeDirection::Falling)
            high = !high;
        s.reference_channel.push_back(high ? 1.0 : 0.0);
        s.sample_channel.push_back(std::sin(2.0 * pi * t / period));
    }
    return s;
}

Segment calibration(int id, double frequency = 10.0, double duty = 50.0, double rate = 1000.0) {
    Segment s;
    s.id = id;
    s.acquisition_order = 0;
    s.sample_rate_hz = rate;
    s.reference_calibration = true;
    const double period = 1.0 / frequency;
    const int count = static_cast<int>(2.2 * period * rate);
    for (int i = 0; i < count; ++i) {
        double phase = std::fmod(i / rate, period) / period;
        s.reference_channel.push_back(phase < duty / 100.0 ? 1.0 : 0.0);
        s.sample_channel.push_back(0.0);
    }
    return s;
}

Segment slow_edge(int id, int order, bool falling) {
    Segment s;
    s.id = id;
    s.acquisition_order = order;
    s.sample_rate_hz = 2.0;
    for (int i = 0; i < 32; ++i) {
        const double relative = (i - 16) / 2.0;
        const bool high = falling ? relative < 0.0 : relative >= 0.0;
        s.reference_channel.push_back(high ? 1.0 : 0.0);
        double phase = (falling ? 0.5 : 0.0) + relative / 50.0;
        phase -= std::floor(phase);
        s.sample_channel.push_back(std::sin(2 * pi * phase));
    }
    return s;
}

Segment slow_edge_at(int id, int order, bool falling, double edge_seconds) {
    Segment s;
    s.id = id;
    s.acquisition_order = order;
    s.sample_rate_hz = 10.0;
    for (int i = 0; i < 160; ++i) {
        const double relative = i / 10.0 - edge_seconds;
        const bool high = falling ? relative < 0.0 : relative >= 0.0;
        s.reference_channel.push_back(high ? 1.0 : 0.0);
        double phase = (falling ? 0.5 : 0.0) + relative / 50.0;
        phase -= std::floor(phase);
        s.sample_channel.push_back(std::sin(2 * pi * phase));
    }
    return s;
}

void expect_code(const std::vector<Segment>& segments, StitchConfig c, const char* code,
                 ExitCategory category = ExitCategory::ValidationFailed) {
    std::vector<Segment> input = segments;
    if (std::string(code) != "REFERENCE_CALIBRATION_REQUIRED")
        input.insert(input.begin(), calibration(999, c.signal_frequency_hz));
    if (c.max_attempts < static_cast<int>(input.size()) && std::string(code) != "MAX_ATTEMPTS_EXCEEDED")
        c.max_attempts = static_cast<int>(input.size());
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.status == Status::Fail);
    assert(r.command.code == code);
    assert(r.command.exit_category == category);
    assert(r.waveforms.empty());
}

void test_edges_and_period_validation() {
    StitchConfig c = config();
    std::vector<Segment> rising_input;
    rising_input.push_back(calibration(99));
    rising_input.push_back(referenced(1, EdgeDirection::Rising));
    StitchResult rising = stitch_phase_waveforms(rising_input, c);
    assert(rising.command.status == Status::Pass);
    c.edge_direction = EdgeDirection::Falling;
    std::vector<Segment> falling_input;
    falling_input.push_back(calibration(99));
    falling_input.push_back(referenced(1, EdgeDirection::Falling));
    StitchResult falling = stitch_phase_waveforms(falling_input, c);
    assert(falling.command.status == Status::Pass);

    Segment low = referenced(1, EdgeDirection::Rising);
    for (std::size_t i = 0; i < low.reference_channel.size(); ++i)
        low.reference_channel[i] *= 0.2;
    expect_code(std::vector<Segment>(1, low), config(), "REFERENCE_SPAN_TOO_LOW");
    expect_code(std::vector<Segment>(1, referenced(1, EdgeDirection::Rising, 0.08)), config(),
                "REFERENCE_FREQUENCY_MISMATCH");
    StitchConfig jitter = config();
    jitter.max_edge_jitter_us = 50.0;
    expect_code(std::vector<Segment>(1, referenced(1, EdgeDirection::Rising, 0.1, 0.002)), jitter,
                "EDGE_JITTER_EXCEEDED");
}

void test_phase_sources() {
    Segment none = known(1, 1, 0.0);
    none.has_known_phase = false;
    expect_code(std::vector<Segment>(1, none), config(), "SEGMENT_PHASE_UNKNOWN");
    Segment delayed = known(1, 1, 0.5);
    delayed.known_phase = 0.25;
    delayed.trigger_delay_seconds = 0.025;
    std::vector<Segment> delayed_input;
    delayed_input.push_back(calibration(99));
    delayed_input.push_back(delayed);
    StitchResult r = stitch_phase_waveforms(delayed_input, config());
    assert(r.command.status == Status::Pass);
    assert(r.waveforms[0].evidence.at("phase_source") == "metadata");

    Segment single = referenced(1, EdgeDirection::Rising);
    single.reference_channel.resize(149);
    single.sample_channel.resize(149);
    std::vector<Segment> single_input;
    single_input.push_back(calibration(99));
    single_input.push_back(single);
    r = stitch_phase_waveforms(single_input, config());
    assert(r.command.status == Status::Pass);
    assert(r.waveforms[0].evidence.at("period_source") == "estimated");
}

void test_complete_and_independent_waveforms() {
    std::vector<Segment> segments;
    segments.push_back(known(2, 2, 0.0));
    segments.push_back(known(1, 1, 0.0));
    StitchConfig c = config();
    c.requested_waveforms = 2;
    segments.insert(segments.begin(), calibration(99));
    StitchResult r = stitch_phase_waveforms(segments, c);
    assert(r.command.status == Status::Pass && r.command.code == "N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED");
    assert(r.waveforms.size() == 2);
    std::set<int> ids0(r.waveforms[0].segment_ids.begin(), r.waveforms[0].segment_ids.end());
    for (std::size_t i = 0; i < r.waveforms[1].segment_ids.size(); ++i)
        assert(!ids0.count(r.waveforms[1].segment_ids[i]));
    for (std::size_t w = 0; w < r.waveforms.size(); ++w) {
        assert(r.waveforms[w].coverage_percent == 100.0);
        assert(r.waveforms[w].values.size() == 10);
    }
    assert(r.command.evidence.at("requested_waveforms") == "2");
    assert(r.command.evidence.at("reconstructed_waveforms") == "2");
    assert(!r.command.evidence.at("max_overlap_error_v").empty());
    assert(!r.command.evidence.at("max_drift_v").empty());
    assert(!r.command.evidence.at("boundary_jump_v").empty());
}

void test_coverage_overlap_and_attempt_failures() {
    Segment partial = known(1, 1, 0.0);
    partial.sample_channel.resize(4);
    expect_code(std::vector<Segment>(1, partial), config(), "EMPTY_PHASE_BINS");
    StitchConfig samples = config();
    samples.min_samples_per_bin = 2;
    expect_code(std::vector<Segment>(1, known(1, 1, 0.0)), samples, "INSUFFICIENT_PHASE_COVERAGE");
    StitchConfig overlap = config();
    overlap.min_overlap_percent = 20.0;
    Segment first = known(1, 1, 0.0);
    first.sample_channel.resize(5);
    Segment second = known(2, 2, 0.5);
    second.sample_channel.resize(5);
    std::vector<Segment> without_overlap;
    without_overlap.push_back(first);
    without_overlap.push_back(second);
    expect_code(without_overlap, overlap, "INSUFFICIENT_SEGMENT_OVERLAP");

    std::vector<Segment> mismatch;
    mismatch.push_back(known(1, 1, 0.0));
    mismatch.push_back(known(2, 2, 0.0, 1.0));
    StitchConfig mc = config();
    mc.min_samples_per_bin = 2;
    mc.max_overlap_error_v = 0.1;
    expect_code(mismatch, mc, "OVERLAP_MISMATCH");

    StitchConfig complete = config();
    complete.requested_waveforms = 2;
    expect_code(std::vector<Segment>(1, known(1, 1, 0.0)), complete, "INSUFFICIENT_COMPLETE_WAVEFORMS");
    complete.max_attempts = 1;
    expect_code(std::vector<Segment>(1, known(1, 1, 0.0)), complete, "MAX_ATTEMPTS_EXCEEDED");
}

void test_response_and_boundary_validation() {
    std::vector<Segment> drift;
    drift.push_back(known(1, 1, 0.0));
    drift.push_back(known(2, 2, 0.0, 0.4));
    StitchConfig dc = config();
    dc.min_samples_per_bin = 2;
    dc.max_overlap_error_v = 1.0;
    dc.max_response_drift_v = 0.2;
    expect_code(drift, dc, "NON_STATIONARY_RESPONSE");

    Segment boundary = known(1, 1, 0.0);
    boundary.sample_channel.front() = 3.0;
    StitchConfig bc = config();
    bc.max_boundary_jump_v = 0.5;
    expect_code(std::vector<Segment>(1, boundary), bc, "WAVEFORM_BOUNDARY_DISCONTINUITY");
}

void test_real_slow_period_and_duty_phase() {
    StitchConfig c = config();
    c.signal_frequency_hz = 0.02;
    c.phase_bin_count = 100;
    c.max_edge_jitter_us = 1e6;
    c.max_overlap_error_v = 2.0;
    c.max_response_drift_v = 2.0;
    std::vector<Segment> both;
    both.push_back(slow_edge_at(1, 1, false, 1));
    both.push_back(slow_edge_at(2, 2, false, 15));
    both.push_back(slow_edge_at(3, 3, true, 1));
    both.push_back(slow_edge_at(4, 4, true, 15));
    both.insert(both.begin(), calibration(99, 0.02, 50, 2));
    c.max_attempts = 10;
    StitchResult r = stitch_phase_waveforms(both, c);
    assert(r.command.status == Status::Pass && r.command.evidence.at("calibration_measured_period_seconds") == "50");
    expect_code(std::vector<Segment>(1, slow_edge_at(1, 1, false, 8)), c, "INSUFFICIENT_PHASE_COVERAGE");
    StitchConfig duty = config();
    duty.reference_duty_cycle_percent = 40.0;
    expect_code(std::vector<Segment>(1, referenced(3, EdgeDirection::Rising)), duty, "REFERENCE_DUTY_CYCLE_MISMATCH");
}

void test_global_stability_attempts_and_ids() {
    std::vector<Segment> shifted;
    shifted.push_back(known(1, 1, 0));
    shifted.push_back(known(2, 2, 0, 0.4, 1.5));
    StitchConfig c = config();
    c.requested_waveforms = 2;
    c.max_response_drift_v = 0.2;
    expect_code(shifted, c, "NON_STATIONARY_RESPONSE");
    c = config();
    c.max_attempts = 1;
    std::vector<Segment> input;
    Segment useless = known(1, 1, 0);
    useless.sample_channel.resize(2);
    input.push_back(useless);
    input.push_back(known(2, 2, 0));
    expect_code(input, c, "MAX_ATTEMPTS_EXCEEDED");
    input.clear();
    input.push_back(known(1, 1, 0));
    input.push_back(known(1, 2, 0));
    expect_code(input, config(), "INVALID_SEGMENT_ID", ExitCategory::InvalidArguments);
    Segment zero = known(0, 1, 0);
    expect_code(std::vector<Segment>(1, zero), config(), "INVALID_SEGMENT_ID", ExitCategory::InvalidArguments);
}

void test_timestamp_rate_consistency() {
    Segment s = known(1, 1, 0);
    s.timestamps.resize(10);
    for (int i = 0; i < 10; ++i)
        s.timestamps[i] = i / 99.0;
    expect_code(std::vector<Segment>(1, s), config(), "INVALID_SEGMENT", ExitCategory::InvalidArguments);
}

void test_reference_calibration_is_required_and_not_reconstructed() {
    expect_code(std::vector<Segment>(1, known(1, 1, 0)), config(), "REFERENCE_CALIBRATION_REQUIRED");
    std::vector<Segment> input;
    input.push_back(calibration(90));
    input.push_back(known(1, 1, 0));
    StitchResult r = stitch_phase_waveforms(input, config());
    assert(r.command.status == Status::Pass);
    assert(r.waveforms[0].segment_ids.size() == 1 && r.waveforms[0].segment_ids[0] == 1);
    assert(r.command.evidence.at("calibration_segment_id") == "90");
}

void test_invalid_segments_are_collected_before_success() {
    Segment invalid = known(2, 2, 0);
    invalid.has_known_phase = false;
    std::vector<Segment> input;
    input.push_back(calibration(90));
    input.push_back(invalid);
    input.push_back(known(3, 3, 0));
    StitchConfig c = config();
    c.max_attempts = 3;
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.status == Status::Pass && r.command.evidence.at("invalid_segment_count") == "1");
    assert(r.command.evidence.at("invalid_0_id") == "2");
}

void test_drift_components_are_reported() {
    std::vector<Segment> input;
    input.push_back(calibration(90));
    input.push_back(known(1, 1, 0));
    input.push_back(known(2, 2, 0, 0.4));
    StitchConfig c = config();
    c.requested_waveforms = 2;
    c.max_attempts = 3;
    c.max_response_drift_v = 0.2;
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.code == "NON_STATIONARY_RESPONSE");
    assert(r.command.evidence.at("baseline_drift_v") == "0.4");
    assert(r.command.evidence.count("amplitude_drift_v") && r.command.evidence.count("shape_drift_v") &&
           r.command.evidence.count("trend_drift_v"));
}

void test_amplitude_shape_and_trend_drift_components() {
    StitchConfig c = config();
    c.requested_waveforms = 2;
    c.max_attempts = 3;
    c.max_response_drift_v = 0.2;
    std::vector<Segment> amplitude;
    amplitude.push_back(calibration(90));
    amplitude.push_back(known(1, 1, 0));
    amplitude.push_back(known(2, 2, 0, 0, 1.3));
    StitchResult r = stitch_phase_waveforms(amplitude, c);
    assert(r.command.code == "NON_STATIONARY_RESPONSE");
    assert(std::atof(r.command.evidence.at("amplitude_drift_v").c_str()) > 0.2);
    Segment shaped = known(2, 2, 0);
    shaped.sample_channel[2] += 0.35;
    shaped.sample_channel[7] -= 0.35;
    std::vector<Segment> shape;
    shape.push_back(calibration(90));
    shape.push_back(known(1, 1, 0));
    shape.push_back(shaped);
    r = stitch_phase_waveforms(shape, c);
    assert(r.command.code == "NON_STATIONARY_RESPONSE");
    assert(std::atof(r.command.evidence.at("shape_drift_v").c_str()) > 0.2);
    c.requested_waveforms = 3;
    c.max_attempts = 4;
    std::vector<Segment> trend;
    trend.push_back(calibration(90));
    trend.push_back(known(1, 1, 0, 0));
    trend.push_back(known(2, 2, 0, 0.15));
    trend.push_back(known(3, 3, 0, 0.30));
    r = stitch_phase_waveforms(trend, c);
    assert(r.command.code == "NON_STATIONARY_RESPONSE");
    assert(std::atof(r.command.evidence.at("trend_drift_v").c_str()) > 0.2);
}

void test_measured_calibration_drives_phase_mapping() {
    StitchConfig c = config();
    c.phase_bin_count = 100;
    c.max_frequency_error_percent = 5;
    c.max_duty_cycle_error_percent = 2;
    c.max_boundary_jump_v = 1000;
    c.max_overlap_error_v = 1000;
    c.max_response_drift_v = 1000;
    c.max_attempts = 2;
    Segment samples;
    samples.id = 1;
    samples.acquisition_order = 1;
    samples.sample_rate_hz = 1000;
    samples.has_known_phase = true;
    samples.known_phase = 0;
    samples.trigger_delay_seconds = 0.051;
    for (int i = 0; i < 102; ++i)
        samples.sample_channel.push_back(i);
    std::vector<Segment> input;
    input.push_back(calibration(90, 1.0 / 0.102, 48.0, 1000));
    input.push_back(samples);
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.status == Status::Pass);
    assert(r.command.evidence.at("calibration_measured_period_seconds") == "0.102");
    assert(r.waveforms[0].values[0] == 52.0);
}

void test_invalid_evidence_survives_coverage_failure() {
    Segment invalid = known(1, 1, 0);
    invalid.has_known_phase = false;
    Segment partial = known(2, 2, 0);
    partial.sample_channel.resize(2);
    std::vector<Segment> input;
    input.push_back(calibration(90));
    input.push_back(invalid);
    input.push_back(partial);
    StitchConfig c = config();
    c.max_attempts = 4;
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.code == "EMPTY_PHASE_BINS");
    assert(r.command.evidence.at("invalid_0_id") == "1" &&
           r.command.evidence.at("invalid_0_code") == "SEGMENT_PHASE_UNKNOWN");
}

void test_exact_attempt_limit_returns_max_attempts() {
    Segment partial = known(1, 1, 0);
    partial.sample_channel.resize(2);
    std::vector<Segment> input;
    input.push_back(calibration(90));
    input.push_back(partial);
    StitchConfig c = config();
    c.max_attempts = 2;
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.code == "MAX_ATTEMPTS_EXCEEDED");
}

void test_group_drift_uses_all_acquisitions() {
    std::vector<Segment> input;
    input.push_back(calibration(90));
    input.push_back(known(1, 1, 0, 0));
    input.push_back(known(2, 2, 0, 0.4));
    input.push_back(known(3, 3, 0, 0));
    StitchConfig c = config();
    c.min_samples_per_bin = 3;
    c.max_attempts = 4;
    c.max_overlap_error_v = 1;
    c.max_response_drift_v = 0.2;
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.code == "NON_STATIONARY_RESPONSE");
}

Segment sparse_bins(int id, int order, double phase, int samples) {
    Segment s;
    s.id = id;
    s.acquisition_order = order;
    s.sample_rate_hz = 80;
    s.has_known_phase = true;
    s.known_phase = phase;
    for (int i = 0; i < samples; ++i)
        s.sample_channel.push_back(std::sin(2 * pi * (phase + i / 8.0)));
    return s;
}

void test_partition_repairs_greedy_key_segment_choice() {
    std::vector<Segment> input;
    input.push_back(calibration(90));
    input.push_back(sparse_bins(1, 1, 0, 6));
    input.push_back(sparse_bins(4, 2, 0.5, 4));
    input.push_back(sparse_bins(2, 3, 0.75, 2));
    input.push_back(sparse_bins(3, 4, 0, 4));
    StitchConfig c = config();
    c.phase_bin_count = 4;
    c.requested_waveforms = 2;
    c.max_attempts = 5;
    c.max_boundary_jump_v = 2;
    c.max_overlap_error_v = 1;
    c.max_response_drift_v = 1;
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.status == Status::Pass);
    assert(r.waveforms.size() == 2);
}

void test_multiple_calibrations_choose_latest_and_reject_inconsistent() {
    Segment older = calibration(90);
    older.acquisition_order = 0;
    Segment newer = calibration(91);
    newer.acquisition_order = 2;
    Segment data = known(1, 3, 0);
    std::vector<Segment> input;
    input.push_back(newer);
    input.push_back(data);
    input.push_back(older);
    StitchConfig c = config();
    c.max_attempts = 3;
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.status == Status::Pass && r.command.evidence.at("calibration_segment_id") == "91");
    assert(r.command.evidence.at("calibration_segment_ids") == "90,91");
    Segment high = calibration(92, 10.5);
    high.acquisition_order = 4;
    Segment low = calibration(93, 9.5);
    low.acquisition_order = 5;
    input.push_back(high);
    input.push_back(low);
    c.max_attempts = 5;
    c.max_frequency_error_percent = 6;
    r = stitch_phase_waveforms(input, c);
    assert(r.command.code == "REFERENCE_CALIBRATION_INCONSISTENT");
}

void test_interpolated_schmitt_edges_and_noise_debounce() {
    StitchConfig c = config();
    c.signal_frequency_hz = 1;
    c.max_edge_jitter_us = 1;
    c.max_attempts = 2;
    Segment cal;
    cal.id = 90;
    cal.acquisition_order = 0;
    cal.sample_rate_hz = 10;
    cal.reference_calibration = true;
    for (int i = 0; i < 21; ++i) {
        const double t = i / 10.0, phase = std::fmod(t, 1.0);
        double v;
        if (phase <= 0.2)
            v = 0;
        else if (phase < 0.3)
            v = (phase - 0.2) / 0.1;
        else if (phase <= 0.7)
            v = 1;
        else if (phase < 0.8)
            v = 1 - (phase - 0.7) / 0.1;
        else
            v = 0;
        cal.reference_channel.push_back(v);
        cal.sample_channel.push_back(0);
    }
    Segment data = known(1, 1, 0);
    data.sample_rate_hz = 10;
    std::vector<Segment> input;
    input.push_back(cal);
    input.push_back(data);
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.status == Status::Pass);
    assert(r.command.evidence.at("calibration_measured_period_seconds") == "1");
    assert(r.command.evidence.at("edge_count") == "4");
    Segment clean = calibration(91);
    data = known(2, 1, 0);
    input.clear();
    input.push_back(clean);
    input.push_back(data);
    c = config();
    c.max_attempts = 2;
    r = stitch_phase_waveforms(input, c);
    assert(r.command.status == Status::Pass);
    const std::string clean_edges = r.command.evidence.at("edge_count");
    Segment noisy = calibration(92);
    noisy.reference_channel[60] = 0.52;
    noisy.reference_channel[61] = 0.48;
    noisy.reference_channel[62] = 0.52;
    input.clear();
    input.push_back(noisy);
    input.push_back(data);
    r = stitch_phase_waveforms(input, c);
    assert(r.command.status == Status::Pass);
    assert(r.command.evidence.at("edge_count") == clean_edges);
}

void test_inconsistent_calibration_preserves_prior_invalid_evidence() {
    Segment invalid = calibration(89);
    invalid.acquisition_order = 0;
    invalid.reference_channel.resize(20);
    invalid.sample_channel.resize(20);
    Segment high = calibration(90, 10.5);
    high.acquisition_order = 1;
    Segment low = calibration(91, 9.5);
    low.acquisition_order = 2;
    Segment data = known(1, 3, 0);
    std::vector<Segment> input;
    input.push_back(invalid);
    input.push_back(high);
    input.push_back(low);
    input.push_back(data);
    StitchConfig c = config();
    c.max_attempts = 4;
    c.max_frequency_error_percent = 6;
    StitchResult r = stitch_phase_waveforms(input, c);
    assert(r.command.code == "REFERENCE_CALIBRATION_INCONSISTENT");
    assert(r.command.evidence.at("invalid_segment_count") == "1");
    assert(r.command.evidence.at("invalid_0_id") == "89");
    assert(r.command.evidence.at("invalid_0_code") == "REFERENCE_CALIBRATION_REQUIRED");
}

void test_invalid_arguments_and_samples() {
    StitchConfig bad = config();
    bad.min_overlap_percent = 0.0;
    expect_code(std::vector<Segment>(1, known(1, 1, 0.0)), bad, "INVALID_STITCH_CONFIG",
                ExitCategory::InvalidArguments);
    bad = config();
    bad.max_overlap_error_v = -1.0;
    expect_code(std::vector<Segment>(1, known(1, 1, 0.0)), bad, "INVALID_STITCH_CONFIG",
                ExitCategory::InvalidArguments);
    Segment nan = known(1, 1, 0.0);
    nan.sample_channel[0] = std::numeric_limits<double>::quiet_NaN();
    expect_code(std::vector<Segment>(1, nan), config(), "INVALID_SEGMENT", ExitCategory::InvalidArguments);
    Segment time = known(1, 1, 0.0);
    time.timestamps.push_back(0.0);
    time.timestamps.push_back(0.2);
    time.timestamps.push_back(0.1);
    expect_code(std::vector<Segment>(1, time), config(), "INVALID_SEGMENT", ExitCategory::InvalidArguments);
}
} // namespace

void test_phase_stitcher() {
    test_edges_and_period_validation();
    test_phase_sources();
    test_complete_and_independent_waveforms();
    test_coverage_overlap_and_attempt_failures();
    test_response_and_boundary_validation();
    test_real_slow_period_and_duty_phase();
    test_global_stability_attempts_and_ids();
    test_timestamp_rate_consistency();
    test_reference_calibration_is_required_and_not_reconstructed();
    test_invalid_segments_are_collected_before_success();
    test_drift_components_are_reported();
    test_amplitude_shape_and_trend_drift_components();
    test_measured_calibration_drives_phase_mapping();
    test_invalid_evidence_survives_coverage_failure();
    test_exact_attempt_limit_returns_max_attempts();
    test_group_drift_uses_all_acquisitions();
    test_partition_repairs_greedy_key_segment_choice();
    test_multiple_calibrations_choose_latest_and_reject_inconsistent();
    test_interpolated_schmitt_edges_and_noise_debounce();
    test_inconsistent_calibration_preserves_prior_invalid_evidence();
    test_invalid_arguments_and_samples();
}
