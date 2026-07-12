#pragma once

#include "daq_capability_test/types.hpp"

#include <string>
#include <stdexcept>
#include <vector>

namespace daq_capability_test {

template <typename T>
class MatrixValue {
public:
    enum class State { Empty, Required, Value };
    MatrixValue() : state_(State::Empty), value_() {}
    static MatrixValue required() { MatrixValue result; result.state_ = State::Required; return result; }
    static MatrixValue from_value(const T& value) { MatrixValue result; result.state_ = State::Value; result.value_ = value; return result; }
    bool is_empty() const { return state_ == State::Empty; }
    bool is_required() const { return state_ == State::Required; }
    bool has_value() const { return state_ == State::Value; }
    const T& value() const {
        if (state_ == State::Empty) throw std::logic_error("MatrixValue is Empty");
        if (state_ == State::Required) throw std::logic_error("MatrixValue is Required");
        return value_;
    }
private:
    State state_;
    T value_;
};

struct MatrixCase {
    std::string case_name;
    bool enabled = false;
    unsigned int line = 0;
    MatrixValue<std::string> device, mode, value_range, trigger_source, trigger_edge, trigger_action, mock_scenario;
    MatrixValue<int> sample_channel, reference_channel, points_per_channel, repeat_count, min_complete_cycles,
        phase_bin_count, min_samples_per_bin, max_attempts;
    MatrixValue<double> sample_rate_hz, timeout_seconds, signal_frequency_hz, min_signal_span_v,
        max_edge_jitter_us, min_overlap_percent, max_overlap_error_v, max_response_drift_v,
        max_boundary_jump_v, max_delay_position_error_us, reference_duty_cycle_percent,
        max_duty_cycle_error_percent;
    MatrixValue<std::vector<int> > target_waveforms, trigger_delay_counts;
    MatrixValue<std::vector<double> > trigger_delay_target_phase_percent;
};

struct Matrix { std::vector<MatrixCase> cases; };

struct MatrixParseError {
    std::string code, message, field, case_name;
    unsigned int line = 0;
};

struct MatrixParseResult {
    Matrix matrix;
    MatrixParseError error;
    bool ok() const { return error.code.empty(); }
};

MatrixParseResult parse_matrix(const std::string& text);
CommandResult preflight(const Matrix& matrix);
CommandResult preflight(const MatrixParseResult& parsed);

}  // namespace daq_capability_test
