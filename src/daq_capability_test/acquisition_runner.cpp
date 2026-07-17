#include "daq_capability_test/acquisition_runner.hpp"
#include "daq_capability_test/range_normalization.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace daq_capability_test {
namespace {
std::string number(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}
std::string number(unsigned int value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

CommandResult result(Status status, ExitCategory category, const std::string& code, const std::string& message = "") {
    CommandResult r;
    r.status = status;
    r.exit_category = category;
    r.code = code;
    r.message = message;
    return r;
}

template <typename T> CommandResult adapter_failure(const AdapterResult<T>& value) {
    CommandResult r = result(value.unsupported ? Status::Skip : Status::Fail,
                             value.unsupported ? ExitCategory::Unsupported : ExitCategory::Driver,
                             value.code.empty() ? "DAQ_CALL_FAILED" : value.code, value.message);
    if (!value.stage.empty())
        r.evidence["stage"] = value.stage;
    if (!value.driver_error.empty())
        r.evidence["driver_error"] = value.driver_error;
    return r;
}

class StopGuard {
  public:
    explicit StopGuard(DaqAdapter& adapter) : adapter_(adapter), armed_(true) {}
    ~StopGuard() noexcept { stop_now(); }
    void arm() { armed_ = true; }
    void stop_now() noexcept {
        if (armed_) {
            adapter_.stop();
            armed_ = false;
        }
    }

  private:
    DaqAdapter& adapter_;
    bool armed_;
};

bool valid(const MatrixCase& c, std::string& field) {
    if (!c.device.has_value())
        field = "device";
    else if (!c.sample_channel.has_value())
        field = "sample_channel";
    else if (!c.value_range.has_value())
        field = "value_range";
    else if (!c.sample_rate_hz.has_value() || c.sample_rate_hz.value() <= 0.0)
        field = "sample_rate_hz";
    else if (!c.points_per_channel.has_value() || c.points_per_channel.value() <= 0)
        field = "points_per_channel";
    else if (!c.repeat_count.has_value() || c.repeat_count.value() <= 0)
        field = "repeat_count";
    else if (!c.timeout_seconds.has_value() || c.timeout_seconds.value() <= 0.0)
        field = "timeout_seconds";
    else if (c.reference_channel.has_value() && c.reference_channel.value() == c.sample_channel.value())
        field = "reference_channel";
    else if (c.signal_frequency_hz.has_value() && c.signal_frequency_hz.value() <= 0.0)
        field = "signal_frequency_hz";
    else if (c.min_complete_cycles.has_value() && c.min_complete_cycles.value() < 0)
        field = "min_complete_cycles";
    else if (c.min_signal_span_v.has_value() && c.min_signal_span_v.value() < 0.0)
        field = "min_signal_span_v";
    return field.empty();
}

CommandResult validate_data(const AcquisitionData& data, const AcquisitionRequest& request, const MatrixCase& c) {
    if (data.timed_out)
        return result(Status::Fail, ExitCategory::Driver, "TIMEOUT");
    if (data.overrun)
        return result(Status::Fail, ExitCategory::Driver, "OVERRUN");
    if (data.cache_overflow)
        return result(Status::Fail, ExitCategory::Driver, "CACHE_OVERFLOW");
    if (!std::isfinite(data.actual_sample_rate_hz) || data.actual_sample_rate_hz <= 0.0) {
        CommandResult r = result(Status::Fail, ExitCategory::ValidationFailed, "INVALID_ACQUISITION_DATA");
        r.evidence["field"] = "actual_sample_rate_hz";
        return r;
    }
    if (!std::isfinite(data.duration_seconds) || data.duration_seconds <= 0.0) {
        CommandResult r = result(Status::Fail, ExitCategory::ValidationFailed, "INVALID_ACQUISITION_DATA");
        r.evidence["field"] = "duration_seconds";
        return r;
    }
    if (!std::isfinite(data.trigger_wait_seconds) || data.trigger_wait_seconds < 0 ||
        !std::isfinite(data.wall_elapsed_seconds) || data.wall_elapsed_seconds < 0) {
        CommandResult r = result(Status::Fail, ExitCategory::ValidationFailed, "INVALID_ACQUISITION_DATA",
                                 "Trigger wait and wall elapsed time must be finite and non-negative");
        r.evidence["field"] = "trigger_wait_seconds";
        return r;
    }
    for (unsigned int channel = 0; channel < data.channels.size(); ++channel) {
        for (unsigned int sample = 0; sample < data.channels[channel].samples.size(); ++sample) {
            if (!std::isfinite(data.channels[channel].samples[sample])) {
                CommandResult r = result(Status::Fail, ExitCategory::ValidationFailed, "INVALID_ACQUISITION_DATA");
                r.evidence["field"] = "sample";
                r.evidence["channel"] = number(channel);
                r.evidence["sample_index"] = number(sample);
                return r;
            }
        }
    }
    const unsigned int expected_channels = static_cast<unsigned int>(request.channels.size());
    if (data.channels.size() != expected_channels)
        return result(Status::Fail, ExitCategory::ValidationFailed, "CHANNEL_POINT_COUNT_MISMATCH");
    for (unsigned int i = 0; i < expected_channels; ++i) {
        if (data.channels[i].samples.size() != request.points_per_channel) {
            CommandResult r = result(Status::Fail, ExitCategory::ValidationFailed,
                                     expected_channels > 1 ? "CHANNEL_POINT_COUNT_MISMATCH" : "POINT_COUNT_MISMATCH");
            r.evidence["requested_points_per_channel"] = number(request.points_per_channel);
            r.evidence["actual_points_per_channel"] =
                number(static_cast<unsigned int>(data.channels[i].samples.size()));
            r.evidence["failed_channel_index"] = number(i);
            std::string counts;
            for (unsigned int channel = 0; channel < expected_channels; ++channel) {
                if (!counts.empty())
                    counts += ",";
                counts += number(static_cast<unsigned int>(data.channels[channel].samples.size()));
            }
            r.evidence["channel_point_counts"] = counts;
            return r;
        }
    }
    const double rate_error = std::fabs(data.actual_sample_rate_hz - request.sample_rate_hz) / request.sample_rate_hz;
    if (rate_error > 1e-9)
        return result(Status::Fail, ExitCategory::ValidationFailed, "CONFIG_READBACK_MISMATCH");
    const double theoretical = static_cast<double>(request.points_per_channel) / request.sample_rate_hz;
    const double duration_error = std::fabs(data.duration_seconds - theoretical) / theoretical;
    if (duration_error > 0.010000000001)
        return result(Status::Fail, ExitCategory::ValidationFailed, "DURATION_OUT_OF_TOLERANCE");
    int complete_cycles = 0;
    if (c.signal_frequency_hz.has_value() && c.min_complete_cycles.has_value()) {
        const int cycles = static_cast<int>(std::floor(data.duration_seconds * c.signal_frequency_hz.value()));
        complete_cycles = cycles;
        if (cycles < c.min_complete_cycles.value())
            return result(Status::Fail, ExitCategory::ValidationFailed, "WINDOW_TOO_SHORT");
    }
    double signal_span = 0.0;
    if (c.min_signal_span_v.has_value()) {
        const std::vector<double>& samples = data.channels[0].samples;
        const std::pair<std::vector<double>::const_iterator, std::vector<double>::const_iterator> bounds =
            std::minmax_element(samples.begin(), samples.end());
        signal_span = *bounds.second - *bounds.first;
        if (signal_span < c.min_signal_span_v.value())
            return result(Status::Fail, ExitCategory::ValidationFailed, "SIGNAL_SPAN_TOO_LOW");
    }
    CommandResult passed = result(Status::Pass, ExitCategory::Success, "");
    passed.evidence["actual_points_per_channel"] = number(request.points_per_channel);
    passed.evidence["measured_duration_seconds"] = number(data.duration_seconds);
    passed.evidence["duration_error_percent"] = number(duration_error * 100.0);
    passed.evidence["actual_sample_rate_hz"] = number(data.actual_sample_rate_hz);
    if (c.signal_frequency_hz.has_value())
        passed.evidence["complete_cycles"] = number(static_cast<unsigned int>(complete_cycles));
    if (c.min_signal_span_v.has_value())
        passed.evidence["signal_span_v"] = number(signal_span);
    return passed;
}

CommandResult unsupported(const std::string& code) { return result(Status::Skip, ExitCategory::Unsupported, code); }

CommandResult driver_exception(const std::string& stage, const std::string& message) {
    CommandResult failure = result(Status::Fail, ExitCategory::Driver, "DRIVER_EXCEPTION", message);
    failure.evidence["stage"] = stage;
    return failure;
}

CommandResult check_capabilities(const CapabilityInfo& capabilities, const AcquisitionRequest& request) {
    if (!capabilities.supports_acquisition)
        return unsupported("ACQUISITION_UNSUPPORTED");
    if (request.channels.size() > capabilities.max_channels)
        return unsupported("CHANNEL_COUNT_UNSUPPORTED");
    for (std::size_t i = 0; i < request.channels.size(); ++i)
        if (request.channels[i] < 0 || static_cast<unsigned int>(request.channels[i]) >= capabilities.max_channels)
            return unsupported("CHANNEL_INDEX_UNSUPPORTED");
    if ((capabilities.min_sample_rate_hz > 0.0 && request.sample_rate_hz < capabilities.min_sample_rate_hz) ||
        (capabilities.max_sample_rate_hz > 0.0 && request.sample_rate_hz > capabilities.max_sample_rate_hz))
        return unsupported("SAMPLE_RATE_UNSUPPORTED");
    if ((capabilities.max_points_per_channel > 0 && request.points_per_channel > capabilities.max_points_per_channel) ||
        (capabilities.buffer_capacity > 0 &&
         request.points_per_channel * request.channels.size() > capabilities.buffer_capacity))
        return unsupported("POINT_COUNT_UNSUPPORTED");
    if (!capabilities.supported_ranges.empty()) {
        bool matched = false;
        for (size_t i = 0; i < capabilities.supported_ranges.size(); ++i)
            if (equivalent_voltage_range(capabilities.supported_ranges[i], request.value_range))
                matched = true;
        if (!matched)
            return unsupported("RANGE_UNSUPPORTED");
    }
    return result(Status::Pass, ExitCategory::Success, "");
}
} // namespace

CommandResult run_acquisition_case(DaqAdapter& adapter, const MatrixCase& c, const AcquisitionObserver& observer) {
    Matrix matrix;
    matrix.cases.push_back(c);
    CommandResult preflight_result = preflight(matrix);
    if (preflight_result.status != Status::Pass)
        return preflight_result;
    std::string invalid_field;
    if (!valid(c, invalid_field)) {
        CommandResult r = result(Status::Fail, ExitCategory::InvalidArguments, "INVALID_ACQUISITION_ARGUMENT");
        r.evidence["field"] = invalid_field;
        return r;
    }

    AcquisitionRequest request;
    request.device = c.device.value();
    request.channels.push_back(c.sample_channel.value());
    if (c.reference_channel.has_value())
        request.channels.push_back(c.reference_channel.value());
    request.value_range = c.value_range.value();
    request.sample_rate_hz = c.sample_rate_hz.value();
    request.points_per_channel = static_cast<unsigned int>(c.points_per_channel.value());
    request.timeout_seconds = c.timeout_seconds.value();
    request.mock_scenario = c.mock_scenario.has_value() ? c.mock_scenario.value() : "success";
    request.mock_signal_frequency_hz = c.signal_frequency_hz.has_value() ? c.signal_frequency_hz.value() : 1000.0;
    StopGuard stop(adapter);
    AdapterResult<CapabilityInfo> capabilities;
    try {
        capabilities = adapter.query_capabilities(c.device.value());
    } catch (const std::exception& exception) {
        return driver_exception("query_capabilities", exception.what());
    } catch (...) {
        return driver_exception("query_capabilities", "Unknown driver exception");
    }
    if (!capabilities.success)
        return adapter_failure(capabilities);
    CommandResult capability_check = check_capabilities(capabilities.value, request);
    if (capability_check.status != Status::Pass)
        return capability_check;

    unsigned int passed = 0;
    unsigned int failure_count = 0;
    CommandResult first_failure, last_success;
    for (int repetition = 1; repetition <= c.repeat_count.value(); ++repetition) {
        if (repetition > 1)
            stop.arm();
        CommandResult current;
        bool configured_successfully = false;
        try {
            AdapterResult<OperationInfo> configured = adapter.configure(request);
            if (!configured.success)
                current = adapter_failure(configured);
            else
                configured_successfully = true;
        } catch (const std::exception& exception) {
            current = driver_exception("configure", exception.what());
        } catch (...) {
            current = driver_exception("configure", "Unknown driver exception");
        }
        if (configured_successfully) {
            try {
                AdapterResult<AcquisitionData> acquired = adapter.acquire_once(request);
                current = acquired.success ? validate_data(acquired.value, request, c) : adapter_failure(acquired);
                if (current.status == Status::Pass && observer)
                    current = observer(static_cast<unsigned int>(repetition - 1), acquired.value);
                if (current.status != Status::Pass && current.evidence.find("stage") == current.evidence.end())
                    current.evidence["stage"] = "validation";
            } catch (const std::exception& exception) {
                current = driver_exception("acquire", exception.what());
            } catch (...) {
                current = driver_exception("acquire", "Unknown driver exception");
            }
        }
        stop.stop_now();
        if (current.status == Status::Pass) {
            ++passed;
            last_success = current;
            continue;
        }
        const std::string stage =
            current.evidence.find("stage") == current.evidence.end() ? "configure" : current.evidence.at("stage");
        if (failure_count == 0)
            first_failure = current;
        const std::string prefix = "failure_" + number(failure_count) + "_";
        first_failure.evidence[prefix + "repetition"] = number(static_cast<unsigned int>(repetition));
        first_failure.evidence[prefix + "stage"] = stage;
        first_failure.evidence[prefix + "code"] = current.code;
        first_failure.evidence[prefix + "message"] = current.message;
        ++failure_count;
        if (current.status == Status::Skip)
            break;
    }
    if (failure_count > 0) {
        first_failure.evidence["failed_repetition"] = first_failure.evidence["failure_0_repetition"];
        first_failure.evidence["passed_repetitions"] = number(passed);
        first_failure.evidence["failure_count"] = number(failure_count);
        return first_failure;
    }
    CommandResult success = last_success;
    success.code = (c.signal_frequency_hz.has_value() || c.min_signal_span_v.has_value())
                       ? "LOW_RATE_WINDOW_VALID"
                       : (request.channels.size() > 1 ? "DUAL_CHANNEL_REFERENCE_READY" : "ACQUISITION_STABLE");
    success.evidence["passed_repetitions"] = number(passed);
    return success;
}

CommandResult validate_acquisition_data(const AcquisitionData& data, const AcquisitionRequest& request,
                                        const MatrixCase* matrix_case) {
    MatrixCase empty;
    return validate_data(data, request, matrix_case ? *matrix_case : empty);
}

} // namespace daq_capability_test
