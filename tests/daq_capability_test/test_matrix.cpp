#include "daq_capability_test/matrix.hpp"

#include <cassert>
#include <fstream>
#include <sstream>
#include <limits>
#include <stdexcept>

using namespace daq_capability_test;

namespace {

const char* header = "case_name\tenabled\tdevice\tmode\tsample_channel\treference_channel\tvalue_range\t"
                     "sample_rate_hz\tpoints_per_channel\trepeat_count\ttimeout_seconds\tsignal_frequency_hz\t"
                     "min_complete_cycles\tmin_signal_span_v\tmax_edge_jitter_us\ttarget_waveforms\tphase_bin_count\t"
                     "min_samples_per_bin\tmin_overlap_percent\tmax_overlap_error_v\tmax_response_drift_v\t"
                     "max_boundary_jump_v\tmax_attempts\ttrigger_source\ttrigger_edge\ttrigger_action\t"
                     "trigger_delay_counts\ttrigger_delay_target_phase_percent\tmax_delay_position_error_us\treference_"
                     "duty_cycle_percent\tmax_duty_cycle_error_percent";

std::string valid_row(const std::string& name = "case1") {
    return name + "\ttrue\tPCI-1714,BID#0\tacquire\t0\t1\t-10V~10V\t1000000\t16000000\t3\t20\t"
                  "1000\t2\t1.0\t5.0\t1,2\t16\t4\t90\t0.1\t0.2\t0.3\t2\tsoftware\trising\tstart\t0\t0\t5\t50\t2";
}

MatrixParseResult parse(const std::string& rows) { return parse_matrix(std::string(header) + "\n" + rows + "\n"); }

std::vector<std::string> split_for_test(const std::string& value) {
    std::vector<std::string> result;
    std::string cell;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\t') {
            result.push_back(cell);
            cell.clear();
        } else
            cell += value[i];
    }
    result.push_back(cell);
    return result;
}

void test_comments_and_blank_lines_are_ignored() {
    MatrixParseResult result = parse_matrix("  # comment\n\n" + std::string(header) + "\n" + valid_row() + "\n");
    assert(result.ok() && result.matrix.cases.size() == 1);
}

void test_empty_tsv_fields_are_preserved() {
    std::string row = valid_row();
    const std::string needle = "\t0\t1\t-10V~10V";
    row.replace(row.find(needle), needle.size(), "\t0\t\t-10V~10V");
    MatrixParseResult result = parse(row);
    assert(result.ok() && result.matrix.cases[0].reference_channel.is_empty());
}

void test_all_fields_and_lists_are_parsed() {
    MatrixParseResult result = parse(valid_row());
    assert(result.ok());
    const MatrixCase& value = result.matrix.cases[0];
    assert(value.device.value() == "PCI-1714,BID#0");
    assert(value.sample_rate_hz.value() == 1000000.0);
    assert(value.target_waveforms.value().size() == 2);
    assert(value.max_delay_position_error_us.value() == 5.0);
}

void test_required_reports_field_case_and_line() {
    std::string row = valid_row("low_sample_rate_100k");
    row.replace(row.find("\t1.0\t"), 5, "\tREQUIRED\t");
    MatrixParseResult parsed = parse(row);
    assert(parsed.ok() && parsed.matrix.cases[0].min_signal_span_v.is_required());
    CommandResult result = preflight(parsed);
    assert(result.code == "REQUIRED_THRESHOLD_MISSING");
    assert(result.evidence.at("field") == "min_signal_span_v");
    assert(result.evidence.at("case") == "low_sample_rate_100k");
    assert(result.evidence.at("line") == "2");
}

void test_disabled_required_does_not_block() {
    std::string row = valid_row("low_sample_rate_100k");
    row.replace(row.find("\ttrue\t"), 6, "\tfalse\t");
    row.replace(row.find("\t1.0\t"), 5, "\tREQUIRED\t");
    assert(preflight(parse(row)).status == Status::Pass);
}

void expect_error(const std::string& text, const std::string& code, const std::string& field = "") {
    MatrixParseResult result = parse_matrix(text);
    assert(!result.ok() && result.error.code == code);
    if (!field.empty())
        assert(result.error.field == field);
    CommandResult command = preflight(result);
    assert(command.exit_category == ExitCategory::InvalidArguments && command.code == code);
}

void test_strict_boolean() {
    expect_error(std::string(header) + "\n" + valid_row().replace(valid_row().find("true"), 4, "TRUE"),
                 "INVALID_BOOLEAN", "enabled");
}
void test_integer_complete_consumption() {
    std::string r = valid_row();
    r.replace(r.find("\t16000000\t"), 10, "\t16x\t");
    expect_error(std::string(header) + "\n" + r, "INVALID_INTEGER", "points_per_channel");
}
void test_float_complete_consumption() {
    std::string r = valid_row();
    r.replace(r.find("\t1000\t"), 6, "\t1.2x\t");
    expect_error(std::string(header) + "\n" + r, "INVALID_NUMBER", "signal_frequency_hz");
}
void test_unknown_column() { expect_error("case_name\tenabled\tunknown\nx\ttrue\tx\n", "UNKNOWN_COLUMN", "unknown"); }
void test_duplicate_case_name() {
    expect_error(std::string(header) + "\n" + valid_row("x") + "\n" + valid_row("x"), "DUPLICATE_CASE_NAME",
                 "case_name");
}
void test_missing_header() { expect_error("# only\n\n", "MISSING_HEADER"); }
void test_missing_required_column() { expect_error("case_name\ndevice\n", "MISSING_COLUMN", "enabled"); }
void test_wrong_column_count() { expect_error(std::string(header) + "\ncase1\ttrue", "COLUMN_COUNT_MISMATCH"); }
void test_invalid_enabled() {
    std::string r = valid_row();
    r.replace(r.find("true"), 4, "yes");
    expect_error(std::string(header) + "\n" + r, "INVALID_BOOLEAN", "enabled");
}

void test_preflight_validation() {
    std::string r = valid_row("single_channel_boundary");
    const std::string device = "PCI-1714,BID#0";
    r.replace(r.find(device), device.size(), "");
    CommandResult result = preflight(parse(r));
    assert(result.code == "MISSING_REQUIRED_FIELD" && result.evidence.at("field") == "device");
}

void test_reference_case_requires_distinct_reference_channel() {
    std::string row = valid_row("dual_channel_reference");
    const std::string needle = "\t0\t1\t-10V~10V";
    row.replace(row.find(needle), needle.size(), "\t0\t\t-10V~10V");
    CommandResult result = preflight(parse(row));
    assert(result.code == "MISSING_REQUIRED_FIELD" && result.evidence.at("field") == "reference_channel");
}

void test_default_matrix_parses_and_requires_threshold() {
    std::ifstream input("src/daq_capability_test/default_test_matrix.tsv", std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    const std::string content = text.str();
    MatrixParseResult parsed = parse_matrix(content);
    assert(parsed.ok());
    CommandResult result = preflight(parsed);
    assert(result.code == "REQUIRED_THRESHOLD_MISSING");
    assert(result.evidence.at("case") == "low_sample_rate_500k");
    assert(result.evidence.at("field") == "min_signal_span_v");
    std::istringstream lines(content);
    std::string line;
    unsigned int physical_line = 0, expected_line = 0;
    while (std::getline(lines, line)) {
        ++physical_line;
        if (line.find("low_sample_rate_500k") != std::string::npos)
            expected_line = physical_line;
    }
    std::ostringstream expected;
    expected << expected_line;
    assert(expected_line > 0 && result.evidence.at("line") == expected.str());
}

MatrixCase base_case(const std::string& name, const std::string& mode) {
    MatrixCase c;
    c.case_name = name;
    c.enabled = true;
    c.line = 7;
    c.device = MatrixValue<std::string>::from_value("PCI-1714,BID#0");
    c.mode = MatrixValue<std::string>::from_value(mode);
    return c;
}

void set_acquire(MatrixCase& c) {
    c.sample_channel = MatrixValue<int>::from_value(0);
    c.value_range = MatrixValue<std::string>::from_value("-10V~10V");
    c.sample_rate_hz = MatrixValue<double>::from_value(1);
    c.points_per_channel = MatrixValue<int>::from_value(1);
    c.repeat_count = MatrixValue<int>::from_value(1);
    c.timeout_seconds = MatrixValue<double>::from_value(1);
}

CommandResult check(const MatrixCase& c) {
    Matrix m;
    m.cases.push_back(c);
    return preflight(m);
}

void test_preflight_mode_specific_requirements() {
    assert(check(base_case("preflight", "preflight")).status == Status::Pass);
    assert(check(base_case("device_capability", "capability")).status == Status::Pass);
    MatrixCase acquire = base_case("single_channel_boundary", "acquire");
    set_acquire(acquire);
    assert(check(acquire).status == Status::Pass);
    MatrixCase low = base_case("low_sample_rate_100k", "acquire");
    set_acquire(low);
    assert(check(low).code == "MISSING_REQUIRED_FIELD" && check(low).evidence.at("field") == "signal_frequency_hz");
    MatrixCase dual = base_case("dual_channel_reference", "acquire");
    set_acquire(dual);
    dual.reference_channel = MatrixValue<int>::from_value(1);
    dual.min_signal_span_v = MatrixValue<double>::from_value(1);
    assert(check(dual).status == Status::Pass);
    MatrixCase segment = base_case("segment_phase", "segment_phase");
    set_acquire(segment);
    segment.reference_channel = MatrixValue<int>::from_value(1);
    segment.signal_frequency_hz = MatrixValue<double>::from_value(1);
    segment.min_signal_span_v = MatrixValue<double>::from_value(1);
    segment.max_edge_jitter_us = MatrixValue<double>::from_value(1);
    segment.reference_duty_cycle_percent = MatrixValue<double>::from_value(50);
    segment.max_duty_cycle_error_percent = MatrixValue<double>::from_value(2);
    assert(check(segment).status == Status::Pass);
    MatrixCase stitch = segment;
    stitch.case_name = "phase_stitch";
    stitch.mode = MatrixValue<std::string>::from_value("phase_stitch");
    assert(check(stitch).code == "MISSING_REQUIRED_FIELD" && check(stitch).evidence.at("field") == "target_waveforms");
    MatrixCase trigger = base_case("external_trigger", "trigger");
    set_acquire(trigger);
    trigger.trigger_source = MatrixValue<std::string>::from_value("external");
    trigger.trigger_edge = MatrixValue<std::string>::from_value("rising");
    trigger.trigger_action = MatrixValue<std::string>::from_value("none");
    trigger.max_edge_jitter_us = MatrixValue<double>::from_value(1);
    assert(check(trigger).status == Status::Pass);
    MatrixCase delay = trigger;
    delay.case_name = "delay_trigger";
    delay.mode = MatrixValue<std::string>::from_value("delay_trigger");
    delay.reference_channel = MatrixValue<int>::from_value(1);
    delay.signal_frequency_hz = MatrixValue<double>::from_value(1);
    assert(check(delay).code == "MISSING_REQUIRED_FIELD" &&
           check(delay).evidence.at("field") == "trigger_delay_counts");
}

void test_preflight_rejects_semantic_boundaries() {
    MatrixCase c = base_case("single_channel_boundary", "unknown");
    assert(check(c).evidence.at("field") == "mode");
    c = base_case("single_channel_boundary", "acquire");
    set_acquire(c);
    c.value_range = MatrixValue<std::string>::from_value("10V~10V");
    assert(check(c).evidence.at("field") == "value_range");
    c.value_range = MatrixValue<std::string>::from_value("10V~-10V");
    assert(check(c).evidence.at("field") == "value_range");
    c.value_range = MatrixValue<std::string>::from_value("-10V~10V");
    c.sample_rate_hz = MatrixValue<double>::from_value(0);
    assert(check(c).evidence.at("field") == "sample_rate_hz");
    MatrixCase s = base_case("phase_stitch", "phase_stitch");
    set_acquire(s);
    s.reference_channel = MatrixValue<int>::from_value(1);
    s.signal_frequency_hz = MatrixValue<double>::from_value(1);
    s.min_signal_span_v = MatrixValue<double>::from_value(1);
    s.max_edge_jitter_us = MatrixValue<double>::from_value(1);
    s.reference_duty_cycle_percent = MatrixValue<double>::from_value(50);
    s.max_duty_cycle_error_percent = MatrixValue<double>::from_value(2);
    s.target_waveforms = MatrixValue<std::vector<int>>::from_value(std::vector<int>(1, 1));
    s.phase_bin_count = MatrixValue<int>::from_value(1);
    s.min_samples_per_bin = MatrixValue<int>::from_value(1);
    s.max_attempts = MatrixValue<int>::from_value(2);
    s.min_overlap_percent = MatrixValue<double>::from_value(0);
    s.max_overlap_error_v = MatrixValue<double>::from_value(0);
    s.max_response_drift_v = MatrixValue<double>::from_value(0);
    s.max_boundary_jump_v = MatrixValue<double>::from_value(0);
    assert(check(s).evidence.at("field") == "min_overlap_percent");
    s.min_overlap_percent = MatrixValue<double>::from_value(50);
    s.max_attempts = MatrixValue<int>::from_value(1);
    assert(check(s).evidence.at("field") == "max_attempts");
    s.max_attempts = MatrixValue<int>::from_value(2);
    s.min_overlap_percent = MatrixValue<double>::from_value(50);
    s.reference_duty_cycle_percent = MatrixValue<double>::from_value(100);
    assert(check(s).evidence.at("field") == "reference_duty_cycle_percent");
}

void test_parser_additional_strictness() {
    expect_error("case_name\tenabled\tcase_name\nx\ttrue\tx\n", "DUPLICATE_COLUMN", "case_name");
    std::string r = valid_row();
    r.replace(r.find("\t5\t50\t2"), 2, "\t");
    MatrixParseResult trailing = parse(r);
    assert(trailing.ok() && trailing.matrix.cases[0].max_delay_position_error_us.is_empty());
    r = valid_row();
    r.replace(r.find("\t1,2\t"), 5, "\t,\t");
    expect_error(std::string(header) + "\n" + r, "INVALID_LIST", "target_waveforms");
    r = valid_row();
    r.replace(r.find("\t1000000\t"), 9, "\tnan\t");
    expect_error(std::string(header) + "\n" + r, "INVALID_NUMBER", "sample_rate_hz");
    r = valid_row();
    r.replace(r.find("\t1000000\t"), 9, "\tinf\t");
    expect_error(std::string(header) + "\n" + r, "INVALID_NUMBER", "sample_rate_hz");
    std::vector<std::string> names = split_for_test(header);
    std::string reversed;
    for (std::vector<std::string>::reverse_iterator i = names.rbegin(); i != names.rend(); ++i) {
        if (!reversed.empty())
            reversed += '\t';
        reversed += *i;
    }
    std::vector<std::string> values = split_for_test(valid_row());
    std::string reversed_values;
    for (std::vector<std::string>::reverse_iterator i = values.rbegin(); i != values.rend(); ++i) {
        if (!reversed_values.empty())
            reversed_values += '\t';
        reversed_values += *i;
    }
    assert(parse_matrix(reversed + "\n" + reversed_values + "\n").ok());
}

void test_case_name_is_bound_to_validation_mode() {
    MatrixCase c = base_case("low_sample_rate_100k", "preflight");
    CommandResult mismatch = check(c);
    assert(mismatch.code == "CASE_MODE_MISMATCH");
    assert(mismatch.exit_category == ExitCategory::InvalidArguments);
    assert(mismatch.evidence.at("case") == "low_sample_rate_100k");
    assert(mismatch.evidence.at("expected_mode") == "acquire");
    assert(mismatch.evidence.at("actual_mode") == "preflight");
    assert(mismatch.evidence.at("line") == "7");
    CommandResult unknown = check(base_case("custom_case", "preflight"));
    assert(unknown.code == "UNKNOWN_CASE");
    assert(unknown.evidence.at("case") == "custom_case" && unknown.evidence.at("line") == "7");
    CommandResult prefixed = check(base_case("low_sample_rate_999k", "acquire"));
    assert(prefixed.code == "UNKNOWN_CASE" && prefixed.evidence.at("case") == "low_sample_rate_999k");
}

void test_disabled_cases_still_validate_identity_and_mode() {
    MatrixCase unknown = base_case("disabled_custom", "preflight");
    unknown.enabled = false;
    assert(check(unknown).code == "UNKNOWN_CASE");
    MatrixCase mismatch = base_case("external_trigger", "preflight");
    mismatch.enabled = false;
    assert(check(mismatch).code == "CASE_MODE_MISMATCH");
}

void test_disabled_known_case_skips_all_non_identity_fields() {
    MatrixCase disabled = base_case("low_sample_rate_100k", "acquire");
    disabled.enabled = false;
    disabled.device = MatrixValue<std::string>();
    disabled.sample_rate_hz = MatrixValue<double>::required();
    disabled.min_signal_span_v = MatrixValue<double>::required();
    assert(check(disabled).status == Status::Pass);
}

void test_decimal_parser_rejects_noncanonical_numbers() {
    const char* invalid[] = {" 1.5", "1.5 ", "0x1p2", "1,5"};
    for (size_t i = 0; i < 4; ++i) {
        std::string r = valid_row();
        r.replace(r.find("\t1000\t"), 6, "\t" + std::string(invalid[i]) + "\t");
        expect_error(std::string(header) + "\n" + r, "INVALID_NUMBER", "signal_frequency_hz");
    }
    std::string integer_row = valid_row();
    integer_row.replace(integer_row.find("\t16000000\t"), 10, "\t 1\t");
    expect_error(std::string(header) + "\n" + integer_row, "INVALID_INTEGER", "points_per_channel");
    std::string legal = valid_row();
    legal.replace(legal.find("\t1000\t"), 6, "\t1.5\t");
    assert(parse(legal).ok());
}

void test_matrix_value_rejects_access_without_value() {
    MatrixValue<int> empty;
    bool empty_threw = false;
    try {
        empty.value();
    } catch (const std::logic_error&) {
        empty_threw = true;
    }
    assert(empty.is_empty() && !empty.has_value() && empty_threw);
    MatrixValue<int> required = MatrixValue<int>::required();
    bool required_threw = false;
    try {
        required.value();
    } catch (const std::logic_error&) {
        required_threw = true;
    }
    assert(required.is_required() && !required.has_value() && required_threw);
    MatrixValue<int> value = MatrixValue<int>::from_value(7);
    assert(value.has_value() && value.value() == 7);
}

MatrixCase valid_delay_case() {
    MatrixCase c = base_case("delay_trigger", "delay_trigger");
    set_acquire(c);
    c.sample_rate_hz = MatrixValue<double>::from_value(100000);
    c.points_per_channel = MatrixValue<int>::from_value(30);
    c.reference_channel = MatrixValue<int>::from_value(1);
    c.signal_frequency_hz = MatrixValue<double>::from_value(1000);
    c.trigger_source = MatrixValue<std::string>::from_value("external_analog");
    c.trigger_edge = MatrixValue<std::string>::from_value("rising");
    c.trigger_action = MatrixValue<std::string>::from_value("delay_to_start");
    c.max_edge_jitter_us = MatrixValue<double>::from_value(10);
    c.max_delay_position_error_us = MatrixValue<double>::from_value(5);
    c.reference_duty_cycle_percent = MatrixValue<double>::from_value(50);
    c.max_duty_cycle_error_percent = MatrixValue<double>::from_value(2);
    c.trigger_delay_counts = MatrixValue<std::vector<int>>::from_value(std::vector<int>{0, 250, 500, 750});
    c.trigger_delay_target_phase_percent =
        MatrixValue<std::vector<double>>::from_value(std::vector<double>{0, 25, 50, 75});
    return c;
}

void test_delay_lists_and_coverage_are_strict() {
    MatrixCase c = valid_delay_case();
    assert(check(c).status == Status::Pass);
    c.trigger_delay_target_phase_percent = MatrixValue<std::vector<double>>::from_value(std::vector<double>{0, 25});
    assert(check(c).evidence.at("field") == "trigger_delay_target_phase_percent");
    c = valid_delay_case();
    c.trigger_delay_target_phase_percent =
        MatrixValue<std::vector<double>>::from_value(std::vector<double>{0, 25, 50, 100});
    assert(check(c).evidence.at("field") == "trigger_delay_target_phase_percent");
    c = valid_delay_case();
    c.trigger_delay_counts = MatrixValue<std::vector<int>>::from_value(std::vector<int>{0, 250});
    c.trigger_delay_target_phase_percent = MatrixValue<std::vector<double>>::from_value(std::vector<double>{0, 25});
    assert(check(c).evidence.at("field") == "trigger_delay_target_phase_percent");
    c = valid_delay_case();
    c.points_per_channel = MatrixValue<int>::from_value(100);
    assert(check(c).evidence.at("field") == "points_per_channel");
    c = valid_delay_case();
    c.trigger_delay_target_phase_percent =
        MatrixValue<std::vector<double>>::from_value(std::vector<double>{0, 30.1, 50, 75});
    assert(check(c).evidence.at("field") == "trigger_delay_target_phase_percent");
    std::string row = valid_row();
    size_t p = row.find("\t0\t0\t5\t50\t2");
    row.replace(p, 2, "\t1.5");
    expect_error(std::string(header) + "\n" + row, "INVALID_LIST", "trigger_delay_counts");
}

} // namespace

void test_matrix() {
    test_comments_and_blank_lines_are_ignored();
    test_empty_tsv_fields_are_preserved();
    test_all_fields_and_lists_are_parsed();
    test_required_reports_field_case_and_line();
    test_disabled_required_does_not_block();
    test_strict_boolean();
    test_integer_complete_consumption();
    test_float_complete_consumption();
    test_unknown_column();
    test_duplicate_case_name();
    test_missing_header();
    test_missing_required_column();
    test_wrong_column_count();
    test_invalid_enabled();
    test_preflight_validation();
    test_reference_case_requires_distinct_reference_channel();
    test_default_matrix_parses_and_requires_threshold();
    test_preflight_mode_specific_requirements();
    test_preflight_rejects_semantic_boundaries();
    test_parser_additional_strictness();
    test_case_name_is_bound_to_validation_mode();
    test_disabled_cases_still_validate_identity_and_mode();
    test_decimal_parser_rejects_noncanonical_numbers();
    test_disabled_known_case_skips_all_non_identity_fields();
    test_matrix_value_rejects_access_without_value();
    test_delay_lists_and_coverage_are_strict();
}
