#include "daq_capability_test/suite_runner.hpp"

#include "daq_capability_test/acquisition_runner.hpp"
#include "daq_capability_test/phase_stitcher.hpp"
#include "daq_capability_test/json_result.hpp"
#include "daq_capability_test/result_writer.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <locale>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace daq_capability_test {
namespace {
const char* order[] = {"preflight",       "device_capability",      "single_channel_boundary",
                       "low_sample_rate", "dual_channel_reference", "segment_phase",
                       "phase_stitch",    "external_trigger",       "delay_trigger"};

CommandResult result(Status status, const std::string& code, const std::string& message,
                     ExitCategory category = ExitCategory::ValidationFailed) {
    CommandResult r;
    r.status = status;
    r.code = code;
    r.message = message;
    r.exit_category = category;
    return r;
}

std::vector<std::string> dependencies(const std::string& name) {
    if (name == "device_capability")
        return std::vector<std::string>(1, "preflight");
    if (name == "single_channel_boundary")
        return {"device_capability"};
    if (name == "low_sample_rate")
        return {"device_capability", "single_channel_boundary"};
    if (name == "dual_channel_reference" || name == "external_trigger")
        return {"device_capability"};
    if (name == "segment_phase")
        return {"device_capability", "dual_channel_reference"};
    if (name == "phase_stitch")
        return {"device_capability", "dual_channel_reference", "segment_phase"};
    if (name == "delay_trigger")
        return {"device_capability", "external_trigger"};
    return std::vector<std::string>();
}

const MatrixCase* find_case(const Matrix& matrix, const std::string& name) {
    for (size_t i = 0; i < matrix.cases.size(); ++i)
        if (matrix.cases[i].case_name == name)
            return &matrix.cases[i];
    return 0;
}

std::vector<const MatrixCase*> cases_for(const Matrix& matrix, const std::string& logical) {
    std::vector<const MatrixCase*> found;
    if (logical == "low_sample_rate") {
        const char* names[] = {"low_sample_rate_500k", "low_sample_rate_200k", "low_sample_rate_100k"};
        for (unsigned int i = 0; i < 3; ++i) {
            const MatrixCase* c = find_case(matrix, names[i]);
            if (c)
                found.push_back(c);
        }
    } else {
        const MatrixCase* c = find_case(matrix, logical);
        if (c)
            found.push_back(c);
    }
    return found;
}

void add_with_dependencies(const std::string& name, std::set<std::string>& selected) {
    std::vector<std::string> d = dependencies(name);
    for (size_t i = 0; i < d.size(); ++i)
        add_with_dependencies(d[i], selected);
    selected.insert(name);
}

std::string cause(const SuiteCaseResult& r) {
    std::map<std::string, std::string>::const_iterator it = r.result.evidence.find("prerequisite_code");
    const std::string value = it == r.result.evidence.end() ? r.result.code : it->second;
    return value.empty() ? "NOT_RUN" : value;
}

bool reference_edge(const AcquisitionData& data, const MatrixCase& c, double& seconds) {
    if (data.channels.size() < 2 || data.channels[1].samples.size() < 2)
        return false;
    const std::vector<double>& v = data.channels[1].samples;
    std::pair<std::vector<double>::const_iterator, std::vector<double>::const_iterator> b =
        std::minmax_element(v.begin(), v.end());
    if (*b.second <= *b.first)
        return false;
    double threshold = (*b.first + *b.second) / 2;
    for (size_t i = 1; i < v.size(); ++i) {
        bool crossed = c.trigger_edge.value() == "falling" ? (v[i - 1] > threshold && v[i] <= threshold)
                                                           : (v[i - 1] < threshold && v[i] >= threshold);
        if (crossed) {
            seconds = i / data.actual_sample_rate_hz;
            return true;
        }
    }
    return false;
}

bool reference_phase(const AcquisitionData& data, const MatrixCase& c, double phase, double& actual, double& period) {
    if (data.channels.size() < 2 || data.channels[1].samples.size() < 3 || data.actual_sample_rate_hz <= 0)
        return false;
    const std::vector<double>& v = data.channels[1].samples;
    std::pair<std::vector<double>::const_iterator, std::vector<double>::const_iterator> mm =
        std::minmax_element(v.begin(), v.end());
    if (*mm.second <= *mm.first)
        return false;
    const double threshold = (*mm.first + *mm.second) / 2;
    std::vector<double> edges;
    for (size_t i = 1; i < v.size(); ++i) {
        bool crossed = c.trigger_edge.value() == "falling" ? (v[i - 1] > threshold && v[i] <= threshold)
                                                           : (v[i - 1] < threshold && v[i] >= threshold);
        if (crossed) {
            double fraction = (threshold - v[i - 1]) / (v[i] - v[i - 1]);
            edges.push_back((i - 1 + fraction) / data.actual_sample_rate_hz);
        }
    }
    if (edges.size() < 2)
        return false;
    period = (edges.back() - edges.front()) / (edges.size() - 1);
    if (!(period > 0))
        return false;
    const double delta = edges.front();
    actual = std::fmod(1.0 - delta / period, 1.0);
    if (actual < 0)
        actual += 1.0;
    (void)phase;
    return true;
}

double circular_error_percent(double a, double b) {
    double d = std::fabs(a - b);
    return (std::min)(d, 100.0 - d);
}

StitchConfig stitch_config(const MatrixCase& c, bool full) {
    StitchConfig cfg;
    cfg.signal_frequency_hz = c.signal_frequency_hz.value();
    cfg.phase_bin_count = c.phase_bin_count.has_value() ? c.phase_bin_count.value() : 1;
    cfg.min_samples_per_bin = c.min_samples_per_bin.has_value() ? c.min_samples_per_bin.value() : 1;
    cfg.requested_waveforms = full ? c.target_waveforms.value()[0] : 1;
    cfg.max_attempts = c.max_attempts.has_value() ? c.max_attempts.value() : c.repeat_count.value() + 1;
    cfg.min_reference_span_v = c.min_signal_span_v.value();
    cfg.max_edge_jitter_us = c.max_edge_jitter_us.value();
    cfg.reference_duty_cycle_percent = c.reference_duty_cycle_percent.value();
    cfg.max_duty_cycle_error_percent = c.max_duty_cycle_error_percent.value();
    cfg.min_overlap_percent = c.min_overlap_percent.has_value() ? c.min_overlap_percent.value() : 1.0;
    cfg.max_overlap_error_v = c.max_overlap_error_v.has_value() ? c.max_overlap_error_v.value() : 1e100;
    cfg.max_response_drift_v = c.max_response_drift_v.has_value() ? c.max_response_drift_v.value() : 1e100;
    cfg.max_boundary_jump_v = c.max_boundary_jump_v.has_value() ? c.max_boundary_jump_v.value() : 1e100;
    cfg.max_frequency_error_percent = 5.0;
    return cfg;
}

std::vector<std::string> phase_files(const std::string& directory) {
    std::vector<std::string> files;
#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA((directory + "\\phase_stitch_*.tsv").c_str(), &data);
    if (handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                files.push_back(directory + "\\" + data.cFileName);
        } while (FindNextFileA(handle, &data));
        FindClose(handle);
    }
#else
    DIR* dir = opendir(directory.c_str());
    if (dir) {
        for (dirent* entry = readdir(dir); entry; entry = readdir(dir)) {
            std::string n = entry->d_name;
            if (n.find("phase_stitch_") == 0 && n.size() > 4 && n.substr(n.size() - 4) == ".tsv")
                files.push_back(directory + "/" + n);
        }
        closedir(dir);
    }
#endif
    std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
        const size_t au = a.rfind('_'), ad = a.rfind('.'), bu = b.rfind('_'), bd = b.rfind('.');
        const int ai = au == std::string::npos ? 0 : std::atoi(a.substr(au + 1, ad - au - 1).c_str());
        const int bi = bu == std::string::npos ? 0 : std::atoi(b.substr(bu + 1, bd - bu - 1).c_str());
        return ai < bi;
    });
    return files;
}

bool read_segment(const std::string& path, double rate, int id, Segment& segment) {
    std::ifstream input(path.c_str());
    input.imbue(std::locale::classic());
    std::string header;
    if (!std::getline(input, header) || header.find('\t') == std::string::npos)
        return false;
    segment.id = id + 1;
    segment.acquisition_order = id;
    segment.sample_rate_hz = rate;
    segment.reference_calibration = id == 0;
    size_t index;
    double sample, reference;
    while (input >> index >> sample >> reference) {
        segment.timestamps.push_back(index / rate);
        segment.sample_channel.push_back(sample);
        segment.reference_channel.push_back(reference);
    }
    return !segment.sample_channel.empty() && input.eof();
}

class AdapterStopGuard {
  public:
    explicit AdapterStopGuard(DaqAdapter& adapter) : adapter_(adapter) {}
    ~AdapterStopGuard() noexcept { adapter_.stop(); }

  private:
    DaqAdapter& adapter_;
};
class AdapterExecutor : public CaseExecutor {
  public:
    AdapterExecutor(DaqAdapter& adapter, const Matrix& matrix, ResultWriter* writer = 0,
                    std::vector<SummaryRecord>* summaries = 0)
        : adapter_(adapter), matrix_(matrix), writer_(writer), summaries_(summaries) {}
    CommandResult execute(const MatrixCase& c) override {
        try {
            return execute_impl(c);
        } catch (const std::exception& error) {
            CommandResult failed = result(Status::Fail, "DRIVER_EXCEPTION", error.what(), ExitCategory::Driver);
            failed.evidence["stage"] = "suite_execute";
            return failed;
        } catch (...) {
            CommandResult failed =
                result(Status::Fail, "DRIVER_EXCEPTION", "Unknown driver exception", ExitCategory::Driver);
            failed.evidence["stage"] = "suite_execute";
            return failed;
        }
    }

  private:
    CommandResult execute_impl(const MatrixCase& c) {
        if (c.case_name == "preflight")
            return preflight(matrix_);
        if (c.case_name == "device_capability") {
            AdapterResult<CapabilityInfo> q = adapter_.query_capabilities(c.device.value());
            if (!q.success)
                return result(q.unsupported ? Status::Skip : Status::Fail, q.code, q.message,
                              q.unsupported ? ExitCategory::Unsupported : ExitCategory::Driver);
            return result(Status::Pass, "DEVICE_CAPABILITY_CONFIRMED", "Device capabilities queried",
                          ExitCategory::Success);
        }
        if (c.case_name == "external_trigger" || c.case_name == "delay_trigger") {
            TriggerRequest t;
            t.source = c.trigger_source.value();
            t.edge = c.trigger_edge.value();
            t.mock_scenario = c.mock_scenario.has_value() ? c.mock_scenario.value() : "success";
            t.action = c.trigger_action.value();
            AcquisitionRequest request;
            request.device = c.device.value();
            request.channels.push_back(c.sample_channel.value());
            if (c.reference_channel.has_value())
                request.channels.push_back(c.reference_channel.value());
            request.value_range = c.value_range.value();
            request.sample_rate_hz = c.sample_rate_hz.value();
            request.points_per_channel = c.points_per_channel.value();
            request.timeout_seconds = c.timeout_seconds.value();
            request.mock_scenario = c.mock_scenario.has_value() ? c.mock_scenario.value() : "success";
            request.mock_signal_frequency_hz =
                c.signal_frequency_hz.has_value() ? c.signal_frequency_hz.value() : 1000.0;
            if (c.case_name == "delay_trigger" && !c.reference_channel.has_value())
                return result(Status::Fail, "REFERENCE_CHANNEL_REQUIRED",
                              "Delay position requires a sampled reference channel");
            std::vector<double> edges;
            std::vector<std::vector<double>> delay_probe_phases;
            std::vector<std::pair<double, double>> coverage;
            std::map<std::string, std::string> delay_evidence;
            const size_t delay_points = c.case_name == "delay_trigger" ? c.trigger_delay_counts.value().size() : 1;
            delay_probe_phases.resize(delay_points);
            double calibration_period = 0, calibration_threshold = 0, calibration_duty = 0;
            if (c.case_name == "delay_trigger") {
                AcquisitionRequest calibration = request;
                calibration.points_per_channel =
                    (std::max)(request.points_per_channel,
                               static_cast<unsigned int>(
                                   std::ceil(2.2 * request.sample_rate_hz / c.signal_frequency_hz.value())));
                calibration.role = "delay_calibration";
                TriggerRequest none = t;
                none.action = "none";
                none.delay_counts = 0;
                AdapterStopGuard stop(adapter_);
                AdapterResult<OperationInfo> ac = adapter_.configure(calibration);
                if (!ac.success)
                    return result(Status::Fail, ac.code, ac.message, ExitCategory::Driver);
                AdapterResult<OperationInfo> tc = adapter_.configure_trigger(none);
                if (!tc.success)
                    return result(Status::Fail, tc.code, tc.message, ExitCategory::Driver);
                AdapterResult<AcquisitionData> data = adapter_.acquire_once(calibration);
                if (!data.success)
                    return result(Status::Fail, data.code, data.message, ExitCategory::Driver);
                CommandResult valid = validate_acquisition_data(data.value, calibration, &c);
                if (valid.status != Status::Pass)
                    return valid;
                if (writer_) {
                    std::vector<RawChannel> raw(2);
                    raw[0].name = "sample";
                    raw[0].samples = data.value.channels[0].samples;
                    raw[1].name = "reference";
                    raw[1].samples = data.value.channels[1].samples;
                    CommandResult w = writer_->write_raw("delay_trigger_calibration", 0, raw);
                    if (w.status != Status::Pass)
                        return w;
                }
                if (summaries_) {
                    SummaryRecord s = make_acquisition_summary(c, calibration, 0, data.value, true);
                    s.test_name = "delay_trigger_calibration";
                    summaries_->push_back(s);
                }
                double ignored;
                if (!reference_phase(data.value, c, 0, ignored, calibration_period))
                    return result(Status::Fail, "TRIGGER_REFERENCE_PERIOD_MISSING",
                                  "Calibration requires at least two reference edges");
                const std::vector<double>& rv = data.value.channels[1].samples;
                std::pair<std::vector<double>::const_iterator, std::vector<double>::const_iterator> levels =
                    std::minmax_element(rv.begin(), rv.end());
                calibration_threshold = (*levels.first + *levels.second) / 2;
                double first_edge;
                if (!reference_edge(data.value, c, first_edge))
                    return result(Status::Fail, "TRIGGER_REFERENCE_EDGE_MISSING", "Calibration edge missing");
                size_t begin = static_cast<size_t>(std::ceil(first_edge * data.value.actual_sample_rate_hz)),
                       finish =
                           (std::min)(rv.size(), begin + static_cast<size_t>(std::floor(
                                                             calibration_period * data.value.actual_sample_rate_hz)));
                for (size_t i = begin; i < finish; ++i)
                    if (rv[i] >= calibration_threshold)
                        calibration_duty += 1.0;
                if (finish <= begin)
                    return result(Status::Fail, "TRIGGER_REFERENCE_PERIOD_MISSING",
                                  "Calibration period has no samples");
                calibration_duty /= (finish - begin);
                delay_evidence["calibration_period_seconds"] = std::to_string(calibration_period);
                delay_evidence["calibration_duty_percent"] = std::to_string(calibration_duty * 100);
                if (std::fabs(calibration_duty * 100 - c.reference_duty_cycle_percent.value()) >
                    c.max_duty_cycle_error_percent.value())
                    return result(Status::Fail, "REFERENCE_DUTY_CYCLE_MISMATCH",
                                  "Measured reference duty cycle is outside tolerance");
            }
            for (size_t delay_index = 0; delay_index < delay_points; ++delay_index) {
                t.delay_counts = c.case_name == "delay_trigger" ? c.trigger_delay_counts.value()[delay_index] : 0;
                for (int repetition = 0; repetition < c.repeat_count.value(); ++repetition) {
                    double probe_phase = 0;
                    if (c.case_name == "delay_trigger") {
                        AcquisitionRequest probe = request;
                        probe.points_per_channel =
                            (std::max)(request.points_per_channel,
                                       static_cast<unsigned int>(
                                           std::ceil(2.2 * request.sample_rate_hz / c.signal_frequency_hz.value())));
                        probe.role = "delay_probe";
                        AdapterStopGuard stop(adapter_);
                        AdapterResult<OperationInfo> ac = adapter_.configure(probe);
                        if (!ac.success)
                            return result(Status::Fail, ac.code, ac.message, ExitCategory::Driver);
                        AdapterResult<OperationInfo> tc = adapter_.configure_trigger(t);
                        if (!tc.success)
                            return result(Status::Fail, tc.code, tc.message, ExitCategory::Driver);
                        AdapterResult<AcquisitionData> data = adapter_.acquire_once(probe);
                        if (!data.success)
                            return result(Status::Fail, data.code, data.message, ExitCategory::Driver);
                        CommandResult valid = validate_acquisition_data(data.value, probe, &c);
                        if (valid.status != Status::Pass)
                            return valid;
                        const unsigned int audit =
                            static_cast<unsigned int>(delay_index * c.repeat_count.value() + repetition);
                        if (writer_) {
                            std::vector<RawChannel> raw(2);
                            raw[0].name = "sample";
                            raw[0].samples = data.value.channels[0].samples;
                            raw[1].name = "reference";
                            raw[1].samples = data.value.channels[1].samples;
                            CommandResult w = writer_->write_raw("delay_trigger_probe", audit, raw);
                            if (w.status != Status::Pass)
                                return w;
                        }
                        if (summaries_) {
                            SummaryRecord s = make_acquisition_summary(c, probe, audit, data.value, true);
                            s.test_name = "delay_trigger_probe";
                            s.trigger_delay = t.delay_counts;
                            summaries_->push_back(s);
                        }
                        double probe_period;
                        if (!reference_phase(data.value, c, 0, probe_phase, probe_period))
                            return result(Status::Fail, "TRIGGER_REFERENCE_EDGE_MISSING",
                                          "Delay probe requires reference edges");
                        delay_probe_phases[delay_index].push_back(probe_phase);
                        const double period_error_us = std::fabs(probe_period - calibration_period) * 1e6;
                        if (period_error_us > c.max_edge_jitter_us.value())
                            return result(Status::Fail, "TRIGGER_REFERENCE_PERIOD_DRIFT",
                                          "Probe period differs from calibration");
                        const double frequency_error = std::fabs(1.0 / probe_period - c.signal_frequency_hz.value()) *
                                                       100.0 / c.signal_frequency_hz.value();
                        if (frequency_error > 5.0)
                            return result(Status::Fail, "TRIGGER_REFERENCE_FREQUENCY_MISMATCH",
                                          "Probe frequency differs from configured frequency");
                        const double actual_percent = probe_phase * 100.0,
                                     target = c.trigger_delay_target_phase_percent.value()[delay_index],
                                     error_percent = circular_error_percent(actual_percent, target),
                                     tolerance_percent =
                                         c.max_delay_position_error_us.value() * 100.0 / (calibration_period * 1e6);
                        std::string key =
                            "delay_" + std::to_string(delay_index) + "_repeat_" + std::to_string(repetition) + "_";
                        delay_evidence[key + "count"] = std::to_string(t.delay_counts);
                        delay_evidence[key + "target_phase_percent"] = std::to_string(target);
                        delay_evidence[key + "actual_phase_percent"] = std::to_string(actual_percent);
                        delay_evidence[key + "phase_error_percent"] = std::to_string(error_percent);
                        delay_evidence[key + "probe_period_seconds"] = std::to_string(probe_period);
                        if (error_percent > tolerance_percent) {
                            CommandResult failed = result(Status::Fail, "DELAY_POSITION_MISMATCH",
                                                          "Measured probe start phase is outside tolerance");
                            failed.evidence = delay_evidence;
                            return failed;
                        }
                    }
                    {
                        AdapterStopGuard stop(adapter_);
                        AdapterResult<OperationInfo> ac = adapter_.configure(request);
                        if (!ac.success)
                            return result(Status::Fail, ac.code, ac.message, ExitCategory::Driver);
                        AdapterResult<OperationInfo> configured = adapter_.configure_trigger(t);
                        if (!configured.success)
                            return result(configured.unsupported ? Status::Skip : Status::Fail, configured.code,
                                          configured.message,
                                          configured.unsupported ? ExitCategory::Unsupported : ExitCategory::Driver);
                        AdapterResult<AcquisitionData> data = adapter_.acquire_once(request);
                        if (!data.success)
                            return result(Status::Fail, data.code, data.message, ExitCategory::Driver);
                        CommandResult valid = validate_acquisition_data(data.value, request, &c);
                        if (valid.status != Status::Pass)
                            return valid;
                        if (c.case_name == "delay_trigger") {
                            const std::string timing_key =
                                "delay_" + std::to_string(delay_index) + "_repeat_" + std::to_string(repetition);
                            delay_evidence[timing_key + "_trigger_wait_seconds"] =
                                std::to_string(data.value.trigger_wait_seconds);
                            delay_evidence[timing_key + "_wall_elapsed_seconds"] =
                                std::to_string(data.value.wall_elapsed_seconds);
                        }
                        const unsigned int audit_repetition =
                            static_cast<unsigned int>(delay_index * c.repeat_count.value() + repetition);
                        if (writer_) {
                            std::vector<RawChannel> raw;
                            for (size_t i = 0; i < data.value.channels.size(); ++i) {
                                RawChannel channel;
                                channel.name = "channel_" + std::to_string(i);
                                channel.samples = data.value.channels[i].samples;
                                raw.push_back(channel);
                            }
                            CommandResult written =
                                writer_->write_raw(c.case_name == "delay_trigger" ? "delay_trigger_short" : c.case_name,
                                                   audit_repetition, raw);
                            if (written.status != Status::Pass)
                                return written;
                        }
                        if (summaries_) {
                            SummaryRecord s = make_acquisition_summary(c, request, audit_repetition, data.value, true);
                            s.test_name = c.case_name == "delay_trigger" ? "delay_trigger_short" : c.case_name;
                            s.trigger_delay = t.delay_counts;
                            summaries_->push_back(s);
                        }
                        if (c.reference_channel.has_value()) {
                            double edge;
                            if (reference_edge(data.value, c, edge))
                                edges.push_back(edge);
                            else if (c.case_name != "delay_trigger")
                                return result(Status::Fail, "TRIGGER_REFERENCE_EDGE_MISSING",
                                              "Reference edge was not detected");
                            if (c.case_name == "delay_trigger") {
                                const std::vector<double>& reference = data.value.channels[1].samples;
                                if (reference.empty())
                                    return result(Status::Fail, "TRIGGER_REFERENCE_EDGE_MISSING",
                                                  "Short window has no reference samples");
                                const double tolerance_phase =
                                    c.max_delay_position_error_us.value() / (calibration_period * 1e6);
                                for (size_t i = 0; i < reference.size(); ++i) {
                                    double phase = std::fmod(
                                        probe_phase + i / data.value.actual_sample_rate_hz / calibration_period, 1.0);
                                    double boundary =
                                        c.trigger_edge.value() == "rising" ? calibration_duty : 1.0 - calibration_duty;
                                    double distance =
                                        (std::min)((std::min)(phase, 1.0 - phase), std::fabs(phase - boundary));
                                    if (distance <= tolerance_phase)
                                        continue;
                                    bool expected = c.trigger_edge.value() == "rising"
                                                        ? phase < calibration_duty
                                                        : phase >= 1.0 - calibration_duty;
                                    if ((reference[i] >= calibration_threshold) != expected)
                                        return result(Status::Fail, "DELAY_SHORT_PHASE_INCONSISTENT",
                                                      "Short reference waveform disagrees with probe phase");
                                }
                                double start = probe_phase * 100.0,
                                       window_seconds = reference.size() / data.value.actual_sample_rate_hz,
                                       span = 100.0 * window_seconds / calibration_period, end = start + span;
                                if (end <= 100)
                                    coverage.push_back(std::make_pair(start, end));
                                else {
                                    coverage.push_back(std::make_pair(start, 100.0));
                                    coverage.push_back(std::make_pair(0.0, end - 100.0));
                                }
                            }
                        }
                    }
                }
            }
            if (c.case_name == "delay_trigger") {
                std::sort(coverage.begin(), coverage.end());
                double end = 0, max_gap = 0;
                for (size_t i = 0; i < coverage.size(); ++i) {
                    max_gap = (std::max)(max_gap, coverage[i].first - end);
                    end = (std::max)(end, coverage[i].second);
                }
                max_gap = (std::max)(max_gap, 100.0 - end);
                if (max_gap > 1e-9) {
                    CommandResult failed = result(Status::Fail, "DELAY_WINDOW_INCOMPLETE",
                                                  "Measured delay windows do not cover a full cycle");
                    failed.evidence["max_gap_percent"] = std::to_string(max_gap);
                    return failed;
                }
            }
            if (c.case_name == "delay_trigger") {
                for (size_t d = 0; d < delay_probe_phases.size(); ++d)
                    for (size_t i = 0; i < delay_probe_phases[d].size(); ++i)
                        for (size_t j = i + 1; j < delay_probe_phases[d].size(); ++j) {
                            double spread_us =
                                circular_error_percent(delay_probe_phases[d][i] * 100, delay_probe_phases[d][j] * 100) *
                                calibration_period * 1e4;
                            if (spread_us > c.max_edge_jitter_us.value())
                                return result(Status::Fail, "TRIGGER_START_JITTER_EXCEEDED",
                                              "Repeated probes for one delay exceed jitter tolerance");
                        }
            } else if (edges.size() > 1 &&
                       (*std::max_element(edges.begin(), edges.end()) - *std::min_element(edges.begin(), edges.end())) *
                               1e6 >
                           c.max_edge_jitter_us.value())
                return result(Status::Fail, "TRIGGER_START_JITTER_EXCEEDED", "Trigger start jitter exceeded threshold");
            CommandResult passed =
                result(Status::Pass,
                       c.case_name == "external_trigger" ? "EXTERNAL_TRIGGER_STABLE" : "DELAY_TRIGGER_WINDOW_COVERED",
                       "Trigger validation passed", ExitCategory::Success);
            passed.evidence = delay_evidence;
            return passed;
        }
        if (c.case_name == "phase_stitch")
            return execute_phase(c, true);
        if (c.case_name == "segment_phase")
            return execute_phase(c, false);
        AcquisitionObserver observer;
        if (writer_ || summaries_) {
            observer = [&](unsigned int repetition, const AcquisitionData& data) {
                if (writer_) {
                    std::vector<RawChannel> raw;
                    for (size_t i = 0; i < data.channels.size(); ++i) {
                        RawChannel channel;
                        channel.name = "channel_" + std::to_string(i);
                        channel.samples = data.channels[i].samples;
                        raw.push_back(channel);
                    }
                    CommandResult written = writer_->write_raw(c.case_name, repetition, raw);
                    if (written.status != Status::Pass)
                        return written;
                }
                record(c, repetition, data, false);
                return result(Status::Pass, "ACQUISITION_AUDITED", "Acquisition artifacts recorded",
                              ExitCategory::Success);
            };
        }
        CommandResult acquired = run_acquisition_case(adapter_, c, observer);
        if (acquired.status == Status::Pass)
            acquired.code = default_success_code(c.case_name);
        return acquired;
    }

  private:
    void record(const MatrixCase& c, unsigned int repetition, const AcquisitionData& data, bool trigger) {
        AcquisitionRequest request;
        request.sample_rate_hz = c.sample_rate_hz.value();
        request.points_per_channel = c.points_per_channel.value();
        record(c, request, repetition, data, trigger);
    }
    void record(const MatrixCase& c, const AcquisitionRequest& request, unsigned int repetition,
                const AcquisitionData& data, bool trigger) {
        if (summaries_)
            summaries_->push_back(make_acquisition_summary(c, request, repetition, data, trigger));
    }
    CommandResult execute_phase(const MatrixCase& c, bool full) {
        std::vector<AcquisitionRequest> requests = phase_capture_requests(c);
        std::vector<Segment> segments;
        for (size_t i = 0; i < requests.size(); ++i) {
            AdapterStopGuard stop(adapter_);
            AdapterResult<OperationInfo> configured = adapter_.configure(requests[i]);
            if (!configured.success)
                return result(Status::Fail, configured.code, configured.message, ExitCategory::Driver);
            AdapterResult<AcquisitionData> acquired = adapter_.acquire_once(requests[i]);
            if (!acquired.success)
                return result(acquired.unsupported ? Status::Skip : Status::Fail, acquired.code, acquired.message,
                              acquired.unsupported ? ExitCategory::Unsupported : ExitCategory::Driver);
            CommandResult validated = validate_acquisition_data(acquired.value, requests[i]);
            if (validated.status != Status::Pass)
                return validated;
            if (acquired.value.channels.size() < 2)
                return result(Status::Fail, "CHANNEL_COUNT_MISMATCH", "Two channels required");
            Segment s;
            s.id = static_cast<int>(i) + 1;
            s.acquisition_order = static_cast<int>(i);
            s.sample_rate_hz = acquired.value.actual_sample_rate_hz;
            s.reference_calibration = (i == 0);
            s.sample_channel = acquired.value.channels[0].samples;
            s.reference_channel = acquired.value.channels[1].samples;
            if (writer_) {
                std::vector<RawChannel> raw(2);
                raw[0].name = "sample";
                raw[0].samples = s.sample_channel;
                raw[1].name = "reference";
                raw[1].samples = s.reference_channel;
                CommandResult written = writer_->write_raw(c.case_name, static_cast<unsigned int>(i), raw);
                if (written.status != Status::Pass)
                    return written;
            }
            record(c, requests[i], static_cast<unsigned int>(i), acquired.value, false);
            for (size_t j = 0; j < s.sample_channel.size(); ++j)
                s.timestamps.push_back(j / s.sample_rate_hz);
            segments.push_back(s);
            if (!full && i == 1)
                break;
        }
        StitchConfig cfg = stitch_config(c, full);
        StitchResult stitched = stitch_phase_waveforms(segments, cfg);
        if (stitched.command.status == Status::Pass)
            stitched.command.code = full ? "N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED" : "SEGMENT_PHASE_RESOLVED";
        return stitched.command;
    }
    DaqAdapter& adapter_;
    const Matrix& matrix_;
    ResultWriter* writer_;
    std::vector<SummaryRecord>* summaries_;
};
} // namespace

SummaryRecord make_acquisition_summary(const MatrixCase& c, const AcquisitionRequest& request, unsigned int repetition,
                                       const AcquisitionData& data, bool trigger) {
    SummaryRecord s = {};
    s.test_name = c.case_name;
    s.repetition = repetition;
    s.requested_sample_rate_hz = request.sample_rate_hz;
    s.actual_sample_rate_hz = data.actual_sample_rate_hz;
    s.channel_count = static_cast<unsigned int>(data.channels.size());
    s.requested_points_per_channel = request.points_per_channel;
    s.actual_points_per_channel = data.channels.empty() ? 0 : data.channels[0].samples.size();
    s.expected_duration_seconds = request.points_per_channel / request.sample_rate_hz;
    s.measured_duration_seconds = data.duration_seconds;
    s.trigger_enabled = trigger;
    s.trigger_source = c.trigger_source.has_value() ? c.trigger_source.value() : "";
    s.trigger_edge = c.trigger_edge.has_value() ? c.trigger_edge.value() : "";
    s.trigger_action = c.trigger_action.has_value() ? c.trigger_action.value() : "";
    s.trigger_delay = c.trigger_delay_counts.has_value() && !c.trigger_delay_counts.value().empty()
                          ? c.trigger_delay_counts.value()[0]
                          : 0;
    s.trigger_wait_seconds = data.trigger_wait_seconds;
    s.wall_elapsed_seconds = data.wall_elapsed_seconds;
    s.timed_out = data.timed_out;
    s.overrun = data.overrun;
    s.cache_overflow = data.cache_overflow;
    s.status = "PASS";
    s.code = "ACQUISITION_VALIDATED";
    return s;
}

const SuiteCaseResult& SuiteResult::case_result(const std::string& name) const { return cases.at(name); }

bool known_suite_case(const std::string& name) {
    for (unsigned int i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
        if (name == order[i])
            return true;
    return false;
}

std::string default_success_code(const std::string& name) {
    if (name == "preflight")
        return "PREFLIGHT_OK";
    if (name == "device_capability")
        return "DEVICE_CAPABILITY_CONFIRMED";
    if (name == "single_channel_boundary")
        return "SINGLE_CHANNEL_BOUNDARY_PASSED";
    if (name.find("low_sample_rate") == 0)
        return "LOW_SAMPLE_RATE_PASSED";
    if (name == "dual_channel_reference")
        return "REFERENCE_SIGNAL_VALID";
    if (name == "segment_phase")
        return "SEGMENT_PHASE_RESOLVED";
    if (name == "phase_stitch")
        return "N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED";
    if (name == "external_trigger")
        return "EXTERNAL_TRIGGER_STABLE";
    if (name == "delay_trigger")
        return "DELAY_TRIGGER_WINDOW_COVERED";
    return "PASS";
}

SuiteResult run_suite(const Matrix& matrix, const SuiteScope& scope, CaseExecutor& executor) {
    SuiteResult out;
    std::set<std::string> selected;
    if (scope.kind == SuiteScopeKind::All)
        for (unsigned int i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
            selected.insert(order[i]);
    else if (scope.kind == SuiteScopeKind::Case)
        add_with_dependencies(scope.case_name, selected);
    else {
        bool include = false;
        add_with_dependencies(scope.case_name, selected);
        for (unsigned int i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
            if (scope.case_name == order[i])
                include = true;
            if (include)
                selected.insert(order[i]);
        }
    }
    for (unsigned int i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        const std::string name = order[i];
        if (!selected.count(name))
            continue;
        std::vector<std::string> deps = dependencies(name);
        std::string failed;
        for (size_t j = 0; j < deps.size(); ++j) {
            std::map<std::string, SuiteCaseResult>::const_iterator it = out.cases.find(deps[j]);
            if (it == out.cases.end() || it->second.result.status != Status::Pass) {
                failed = it == out.cases.end() ? "PREREQUISITE_NOT_RUN" : cause(it->second);
                break;
            }
        }
        SuiteCaseResult aggregate;
        if (!failed.empty()) {
            aggregate.result = result(Status::Skip, "PREREQUISITE_FAILED", "Required validation did not pass");
            aggregate.result.evidence["prerequisite_code"] = failed;
        } else {
            std::vector<const MatrixCase*> rows = cases_for(matrix, name);
            if (rows.empty())
                aggregate.result =
                    result(Status::Skip, "CASE_NOT_CONFIGURED", "Matrix case is absent", ExitCategory::Unsupported);
            else {
                bool any_pass = false;
                std::string passed_rates, failed_rates;
                for (size_t j = 0; j < rows.size(); ++j)
                    if (rows[j]->enabled) {
                        aggregate.executed = true;
                        CommandResult one = executor.execute(*rows[j]);
                        if (name == "low_sample_rate" && rows[j]->sample_rate_hz.has_value()) {
                            std::ostringstream rate;
                            rate << rows[j]->sample_rate_hz.value();
                            std::string& list = one.status == Status::Pass ? passed_rates : failed_rates;
                            if (!list.empty())
                                list += ",";
                            list += rate.str();
                        }
                        if (one.status == Status::Pass) {
                            any_pass = true;
                            aggregate.result = one;
                        } else if (aggregate.result.code.empty() || aggregate.result.status != Status::Fail)
                            aggregate.result = one;
                    }
                if (name == "low_sample_rate" && any_pass) {
                    aggregate.result = result(Status::Pass, "LOW_SAMPLE_RATE_PASSED",
                                              "At least one configured low sample rate passed", ExitCategory::Success);
                }
                if (name == "low_sample_rate") {
                    aggregate.result.evidence["passed_sample_rates_hz"] = passed_rates;
                    aggregate.result.evidence["failed_sample_rates_hz"] = failed_rates;
                }
                if (!aggregate.executed)
                    aggregate.result =
                        result(Status::Skip, "CASE_DISABLED", "Matrix case is disabled", ExitCategory::Unsupported);
            }
        }
        out.cases[name] = aggregate;
    }
    const SuiteCaseResult& low = out.cases["low_sample_rate"];
    if (low.result.status == Status::Pass)
        out.supported_strategies.insert("LOW_SAMPLE_RATE");
    else
        out.rejected_strategies["LOW_SAMPLE_RATE"] = cause(low);
    const SuiteCaseResult& phase = out.cases["phase_stitch"];
    if (phase.result.status == Status::Pass && phase.result.code == "N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED")
        out.supported_strategies.insert("PHASE_STITCHING");
    else
        out.rejected_strategies["PHASE_STITCHING"] = cause(phase);
    const SuiteCaseResult& trigger = out.cases["external_trigger"];
    const SuiteCaseResult& delay = out.cases["delay_trigger"];
    if (trigger.result.status == Status::Pass && trigger.result.code == "EXTERNAL_TRIGGER_STABLE" &&
        delay.result.status == Status::Pass && delay.result.code == "DELAY_TRIGGER_WINDOW_COVERED")
        out.supported_strategies.insert("TRIGGER_DELAY");
    else
        out.rejected_strategies["TRIGGER_DELAY"] =
            trigger.result.status == Status::Pass ? cause(delay) : cause(trigger);
    out.command = suite_command_result(out);
    return out;
}

SuiteResult run_suite(DaqAdapter& adapter, const Matrix& matrix, const SuiteScope& scope) {
    AdapterExecutor executor(adapter, matrix);
    return run_suite(matrix, scope, executor);
}
SuiteResult run_suite(DaqAdapter& adapter, const Matrix& matrix, const SuiteScope& scope, ResultWriter* writer) {
    AdapterExecutor executor(adapter, matrix, writer);
    return run_suite(matrix, scope, executor);
}
SuiteResult run_suite(DaqAdapter& adapter, const Matrix& matrix, const SuiteScope& scope, ResultWriter* writer,
                      std::vector<SummaryRecord>* summaries) {
    AdapterExecutor executor(adapter, matrix, writer, summaries);
    return run_suite(matrix, scope, executor);
}

CommandResult run_matrix_case(DaqAdapter& adapter, const Matrix& matrix, const MatrixCase& matrix_case) {
    AdapterExecutor executor(adapter, matrix);
    return executor.execute(matrix_case);
}

CommandResult reconstruct_phase_directory(const std::string& input_directory, const MatrixCase& c) {
    std::ifstream marker((input_directory + "/capture.complete").c_str(), std::ios::binary);
    if (!marker)
        return result(Status::Fail, "RUN_INCOMPLETE", "Capture completion marker is missing",
                      ExitCategory::InvalidArguments);
    std::vector<std::string> files = phase_files(input_directory);
    if (files.empty())
        files = phase_files(input_directory + "\\raw");
    if (files.size() < 2)
        return result(Status::Fail, "RECONSTRUCT_INPUT_INCOMPLETE", "Calibration and at least one segment are required",
                      ExitCategory::InvalidArguments);
    std::vector<Segment> segments;
    for (size_t i = 0; i < files.size(); ++i) {
        const double rate = i == 0 ? (std::min)(c.sample_rate_hz.value(), c.signal_frequency_hz.value() * 100.0)
                                   : c.sample_rate_hz.value();
        Segment s;
        if (!read_segment(files[i], rate, static_cast<int>(i), s))
            return result(Status::Fail, "RECONSTRUCT_INPUT_INVALID", "Saved segment TSV is invalid",
                          ExitCategory::InvalidArguments);
        segments.push_back(s);
    }
    return reconstruct_phase_segments(segments, c);
}

CommandResult reconstruct_phase_segments(const std::vector<Segment>& segments, const MatrixCase& c) {
    StitchResult stitched = stitch_phase_waveforms(segments, stitch_config(c, true));
    if (stitched.command.status == Status::Pass)
        stitched.command.code = "N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED";
    return stitched.command;
}

std::vector<AcquisitionRequest> phase_capture_requests(const MatrixCase& c) {
    AcquisitionRequest base;
    base.device = c.device.value();
    base.channels.push_back(c.sample_channel.value());
    base.channels.push_back(c.reference_channel.value());
    base.value_range = c.value_range.value();
    base.sample_rate_hz = c.sample_rate_hz.value();
    base.points_per_channel = static_cast<unsigned int>(c.points_per_channel.value());
    base.timeout_seconds = c.timeout_seconds.value();
    base.role = "phase_ordinary";
    base.mock_scenario = c.mock_scenario.has_value() ? c.mock_scenario.value() : "success";
    base.mock_signal_frequency_hz = c.signal_frequency_hz.value();
    AcquisitionRequest calibration = base;
    calibration.role = "calibration";
    calibration.sample_rate_hz = (std::min)(base.sample_rate_hz, c.signal_frequency_hz.value() * 100.0);
    calibration.points_per_channel =
        static_cast<unsigned int>(std::ceil(3.0 * calibration.sample_rate_hz / c.signal_frequency_hz.value())) + 1;
    std::vector<AcquisitionRequest> requests(1, calibration);
    const int ordinary_attempts = c.max_attempts.has_value() ? c.max_attempts.value() - 1 : c.repeat_count.value();
    for (int i = 0; i < ordinary_attempts; ++i)
        requests.push_back(base);
    return requests;
}

CommandResult suite_command_result(const SuiteResult& suite) {
    CommandResult r = result(Status::Pass, "SUITE_PASSED", "All selected validations passed", ExitCategory::Success);
    for (std::map<std::string, SuiteCaseResult>::const_iterator it = suite.cases.begin(); it != suite.cases.end();
         ++it) {
        if (it->second.result.code.empty())
            continue;
        r.evidence["case." + it->first] = it->second.result.code;
        r.evidence["case." + it->first + ".message"] = it->second.result.message;
        for (std::map<std::string, std::string>::const_iterator detail = it->second.result.evidence.begin();
             detail != it->second.result.evidence.end(); ++detail)
            r.evidence["case." + it->first + ".evidence." + detail->first] = detail->second;
        if (it->second.result.status == Status::Fail) {
            r.status = Status::Fail;
            r.code = "SUITE_FAILED";
            r.message = "One or more validations failed";
            r.exit_category = it->second.result.exit_category;
        }
    }
    std::ostringstream supported;
    for (std::set<std::string>::const_iterator it = suite.supported_strategies.begin();
         it != suite.supported_strategies.end(); ++it) {
        if (it != suite.supported_strategies.begin())
            supported << ',';
        supported << *it;
    }
    r.evidence["supported_strategies"] = supported.str();
    std::string skipped;
    for (std::map<std::string, SuiteCaseResult>::const_iterator it = suite.cases.begin(); it != suite.cases.end(); ++it)
        if (it->second.result.status == Status::Skip) {
            if (!skipped.empty())
                skipped += ",";
            skipped += it->first;
        }
    r.evidence["skipped_cases"] = skipped;
    std::string rejected;
    for (std::map<std::string, std::string>::const_iterator it = suite.rejected_strategies.begin();
         it != suite.rejected_strategies.end(); ++it)
        r.evidence["rejected_strategy." + it->first] = it->second;
    for (std::map<std::string, std::string>::const_iterator it = suite.rejected_strategies.begin();
         it != suite.rejected_strategies.end(); ++it) {
        if (!rejected.empty())
            rejected += ",";
        rejected += it->first + ":" + it->second;
    }
    r.evidence["rejected_strategies"] = rejected;
    return r;
}

std::string suite_result_json(const SuiteResult& suite) {
    std::string json = to_json(suite.command);
    json.erase(json.size() - 1);
    json += ",\"supported_strategies\":[";
    bool first = true;
    for (std::set<std::string>::const_iterator it = suite.supported_strategies.begin();
         it != suite.supported_strategies.end(); ++it) {
        if (!first)
            json += ',';
        first = false;
        json += json_string(*it);
    }
    json += "],\"rejected_strategies\":{";
    first = true;
    for (std::map<std::string, std::string>::const_iterator it = suite.rejected_strategies.begin();
         it != suite.rejected_strategies.end(); ++it) {
        if (!first)
            json += ',';
        first = false;
        json += json_string(it->first) + ":" + json_string(it->second);
    }
    json += "},\"skipped_cases\":[";
    first = true;
    for (std::map<std::string, SuiteCaseResult>::const_iterator it = suite.cases.begin(); it != suite.cases.end(); ++it)
        if (it->second.result.status == Status::Skip) {
            if (!first)
                json += ',';
            first = false;
            json += json_string(it->first);
        }
    json += "],\"failed_cases\":{";
    first = true;
    for (std::map<std::string, SuiteCaseResult>::const_iterator it = suite.cases.begin(); it != suite.cases.end(); ++it)
        if (it->second.executed && it->second.result.status == Status::Fail && !it->second.result.code.empty()) {
            if (!first)
                json += ',';
            first = false;
            json += json_string(it->first) + ":" + to_json(it->second.result);
        }
    json += "},\"cases\":{";
    first = true;
    for (std::map<std::string, SuiteCaseResult>::const_iterator it = suite.cases.begin(); it != suite.cases.end();
         ++it) {
        if (it->second.result.code.empty())
            continue;
        if (!first)
            json += ',';
        first = false;
        json += json_string(it->first) + ":" + to_json(it->second.result);
    }
    json += "}}";
    return json;
}
} // namespace daq_capability_test
