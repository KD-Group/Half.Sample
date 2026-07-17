#include "daq_capability_test/acquisition_runner.hpp"
#include "daq_capability_test/fake_daq_adapter.hpp"

#include <cassert>
#include <cmath>
#include <stdexcept>
#include <limits>

using namespace daq_capability_test;

namespace {
MatrixCase base_case(int repeats = 1) {
    MatrixCase c;
    c.case_name = "single_channel_boundary";
    c.enabled = true;
    c.device = MatrixValue<std::string>::from_value("DemoDevice,BID#0");
    c.mode = MatrixValue<std::string>::from_value("acquire");
    c.sample_channel = MatrixValue<int>::from_value(0);
    c.value_range = MatrixValue<std::string>::from_value("-10V~10V");
    c.sample_rate_hz = MatrixValue<double>::from_value(100.0);
    c.points_per_channel = MatrixValue<int>::from_value(100);
    c.repeat_count = MatrixValue<int>::from_value(repeats);
    c.timeout_seconds = MatrixValue<double>::from_value(2.0);
    return c;
}

AcquisitionData data(int points = 100, double duration = 1.0, double rate = 100.0) {
    AcquisitionData d;
    d.actual_sample_rate_hz = rate;
    d.duration_seconds = duration;
    d.channels.push_back(ChannelData());
    for (int i = 0; i < points; ++i)
        d.channels[0].samples.push_back(static_cast<double>(i));
    return d;
}

void test_three_successes_and_cleanup() {
    FakeDaqAdapter a;
    a.enqueue_success(data());
    a.enqueue_success(data());
    a.enqueue_success(data());
    CommandResult r = run_acquisition_case(a, base_case(3));
    assert(r.status == Status::Pass && r.code == "ACQUISITION_STABLE");
    assert(r.evidence.at("passed_repetitions") == "3");
    assert(r.evidence.at("actual_points_per_channel") == "100");
    assert(r.evidence.at("measured_duration_seconds") == "1");
    assert(r.evidence.at("duration_error_percent") == "0");
    assert(r.evidence.at("actual_sample_rate_hz") == "100");
    assert(a.configure_call_count() == 3 && a.acquire_call_count() == 3 && a.stop_call_count() == 3);
}

void test_first_failure_keeps_code_and_passed_count() {
    FakeDaqAdapter a;
    a.enqueue_success(data());
    a.enqueue_success(data());
    a.enqueue_failure("CACHE_OVERFLOW", "cache full", "acquire");
    CommandResult r = run_acquisition_case(a, base_case(3));
    assert(r.status == Status::Fail && r.code == "CACHE_OVERFLOW");
    assert(r.exit_category == ExitCategory::Driver);
    assert(r.evidence.at("failed_repetition") == "3");
    assert(r.evidence.at("passed_repetitions") == "2");
    assert(a.stop_call_count() == 3);
}

void test_point_and_channel_counts() {
    FakeDaqAdapter short_read;
    short_read.enqueue_success(data(99));
    CommandResult r = run_acquisition_case(short_read, base_case());
    assert(r.code == "POINT_COUNT_MISMATCH");
    assert(r.evidence.at("requested_points_per_channel") == "100");
    assert(r.evidence.at("actual_points_per_channel") == "99");

    MatrixCase dual = base_case();
    dual.reference_channel = MatrixValue<int>::from_value(1);
    AcquisitionData d = data();
    d.channels.push_back(ChannelData());
    d.channels[1].samples.resize(99);
    FakeDaqAdapter uneven;
    uneven.enqueue_success(d);
    r = run_acquisition_case(uneven, dual);
    assert(r.code == "CHANNEL_POINT_COUNT_MISMATCH");
    assert(r.evidence.at("failed_channel_index") == "1");
    assert(r.evidence.at("channel_point_counts") == "100,99");
}

void test_duration_tolerance_boundary() {
    FakeDaqAdapter exact;
    exact.enqueue_success(data(100, 1.01));
    assert(run_acquisition_case(exact, base_case()).status == Status::Pass);
    FakeDaqAdapter over;
    over.enqueue_success(data(100, 1.01001));
    assert(run_acquisition_case(over, base_case()).code == "DURATION_OUT_OF_TOLERANCE");
}

void test_driver_errors_and_unsupported_are_mapped() {
    const char* codes[] = {"TIMEOUT", "OVERRUN", "CACHE_OVERFLOW"};
    for (int i = 0; i < 3; ++i) {
        FakeDaqAdapter a;
        a.enqueue_failure(codes[i]);
        CommandResult r = run_acquisition_case(a, base_case());
        assert(r.status == Status::Fail && r.exit_category == ExitCategory::Driver && r.code == codes[i]);
        assert(a.stop_call_count() == 1);
    }
    FakeDaqAdapter unsupported;
    unsupported.enqueue_unsupported("DUAL_CHANNEL_UNSUPPORTED");
    CommandResult r = run_acquisition_case(unsupported, base_case());
    assert(r.status == Status::Skip && r.exit_category == ExitCategory::Unsupported);
}

void test_acquisition_event_flags_are_driver_failures() {
    AcquisitionData timeout = data();
    timeout.timed_out = true;
    AcquisitionData overrun = data();
    overrun.overrun = true;
    AcquisitionData overflow = data();
    overflow.cache_overflow = true;
    AcquisitionData values[] = {timeout, overrun, overflow};
    const char* codes[] = {"TIMEOUT", "OVERRUN", "CACHE_OVERFLOW"};
    for (int i = 0; i < 3; ++i) {
        FakeDaqAdapter a;
        a.enqueue_success(values[i]);
        CommandResult r = run_acquisition_case(a, base_case());
        assert(r.code == codes[i] && r.exit_category == ExitCategory::Driver);
    }
}

void test_event_flags_take_priority_over_invalid_payload() {
    AcquisitionData values[3];
    values[0].timed_out = true;
    values[1].overrun = true;
    values[2].cache_overflow = true;
    values[0].actual_sample_rate_hz = std::numeric_limits<double>::quiet_NaN();
    values[1].actual_sample_rate_hz = 0.0;
    values[2].duration_seconds = std::numeric_limits<double>::quiet_NaN();
    const char* codes[] = {"TIMEOUT", "OVERRUN", "CACHE_OVERFLOW"};
    for (int i = 0; i < 3; ++i) {
        FakeDaqAdapter a;
        a.enqueue_success(values[i]);
        CommandResult r = run_acquisition_case(a, base_case());
        assert(r.code == codes[i] && r.exit_category == ExitCategory::Driver);
    }
}

void test_readback_and_low_rate_validation() {
    FakeDaqAdapter mismatch;
    mismatch.enqueue_success(data(100, 1.0, 99.0));
    assert(run_acquisition_case(mismatch, base_case()).code == "CONFIG_READBACK_MISMATCH");

    MatrixCase low = base_case();
    low.case_name = "low_sample_rate_100k";
    low.signal_frequency_hz = MatrixValue<double>::from_value(2.0);
    low.min_complete_cycles = MatrixValue<int>::from_value(3);
    low.min_signal_span_v = MatrixValue<double>::from_value(5.0);
    FakeDaqAdapter too_short;
    too_short.enqueue_success(data(100, 1.0));
    assert(run_acquisition_case(too_short, low).code == "WINDOW_TOO_SHORT");

    low.min_complete_cycles = MatrixValue<int>::from_value(2);
    AcquisitionData flat = data();
    flat.channels[0].samples.assign(100, 1.0);
    FakeDaqAdapter span;
    span.enqueue_success(flat);
    assert(run_acquisition_case(span, low).code == "SIGNAL_SPAN_TOO_LOW");

    AcquisitionData valid = data();
    FakeDaqAdapter pass;
    pass.enqueue_success(valid);
    CommandResult r = run_acquisition_case(pass, low);
    assert(r.status == Status::Pass && r.code == "LOW_RATE_WINDOW_VALID");
    assert(r.evidence.at("complete_cycles") == "2");
    assert(r.evidence.at("signal_span_v") == "99");
}

void test_invalid_matrix_does_not_touch_hardware() {
    MatrixCase invalid = base_case(0);
    FakeDaqAdapter a;
    CommandResult r = run_acquisition_case(a, invalid);
    assert(r.status == Status::Fail && r.exit_category == ExitCategory::InvalidArguments);
    assert(a.configure_call_count() == 0 && a.acquire_call_count() == 0 && a.stop_call_count() == 0);

    MatrixCase required = base_case();
    required.points_per_channel = MatrixValue<int>::required();
    r = run_acquisition_case(a, required);
    assert(r.exit_category == ExitCategory::InvalidArguments && a.configure_call_count() == 0);

    MatrixCase bad_mode = base_case();
    bad_mode.mode = MatrixValue<std::string>::from_value("trigger");
    r = run_acquisition_case(a, bad_mode);
    assert(r.exit_category == ExitCategory::InvalidArguments && a.query_call_count() == 0);
    MatrixCase negative = base_case();
    negative.sample_channel = MatrixValue<int>::from_value(-1);
    r = run_acquisition_case(a, negative);
    assert(r.exit_category == ExitCategory::InvalidArguments && a.query_call_count() == 0);
    MatrixCase bad_range = base_case();
    bad_range.value_range = MatrixValue<std::string>::from_value("invalid");
    r = run_acquisition_case(a, bad_range);
    assert(r.exit_category == ExitCategory::InvalidArguments && a.query_call_count() == 0);
    MatrixCase incomplete_low = base_case();
    incomplete_low.case_name = "low_sample_rate_100k";
    r = run_acquisition_case(a, incomplete_low);
    assert(r.exit_category == ExitCategory::InvalidArguments && a.query_call_count() == 0);
}

CapabilityInfo capability() {
    CapabilityInfo c;
    c.supports_acquisition = true;
    c.max_channels = 2;
    c.min_sample_rate_hz = 10;
    c.max_sample_rate_hz = 1000;
    c.max_points_per_channel = 1000;
    c.buffer_capacity = 2000;
    c.supported_ranges.push_back("-10V~10V");
    return c;
}

void test_capabilities_are_enforced_before_configure() {
    CapabilityInfo caps = capability();
    caps.supports_acquisition = false;
    FakeDaqAdapter unsupported;
    unsupported.enqueue_query_success(caps);
    CommandResult r = run_acquisition_case(unsupported, base_case());
    assert(r.status == Status::Skip && unsupported.configure_call_count() == 0);

    MatrixCase dual = base_case();
    dual.case_name = "dual_channel_reference";
    dual.reference_channel = MatrixValue<int>::from_value(1);
    dual.min_signal_span_v = MatrixValue<double>::from_value(1);
    CapabilityInfo one = capability();
    one.max_channels = 1;
    FakeDaqAdapter channel;
    channel.enqueue_query_success(one);
    assert(run_acquisition_case(channel, dual).code == "CHANNEL_COUNT_UNSUPPORTED" &&
           channel.configure_call_count() == 0);
    MatrixCase bad_index = base_case();
    bad_index.sample_channel = MatrixValue<int>::from_value(2);
    FakeDaqAdapter index;
    index.enqueue_query_success(capability());
    assert(run_acquisition_case(index, bad_index).code == "CHANNEL_INDEX_UNSUPPORTED" &&
           index.configure_call_count() == 0);

    CapabilityInfo low_rate = capability();
    low_rate.min_sample_rate_hz = 101;
    FakeDaqAdapter rate;
    rate.enqueue_query_success(low_rate);
    assert(run_acquisition_case(rate, base_case()).code == "SAMPLE_RATE_UNSUPPORTED" &&
           rate.configure_call_count() == 0);
    CapabilityInfo few_points = capability();
    few_points.max_points_per_channel = 99;
    FakeDaqAdapter points;
    points.enqueue_query_success(few_points);
    assert(run_acquisition_case(points, base_case()).code == "POINT_COUNT_UNSUPPORTED" &&
           points.configure_call_count() == 0);
    CapabilityInfo no_range = capability();
    no_range.supported_ranges.clear();
    no_range.supported_ranges.push_back("0V~10V");
    FakeDaqAdapter range;
    range.enqueue_query_success(no_range);
    assert(run_acquisition_case(range, base_case()).code == "RANGE_UNSUPPORTED" && range.configure_call_count() == 0);
}

void test_fake_stage_queues_and_request_records() {
    FakeDaqAdapter query;
    query.enqueue_query_failure("QUERY_FAILED", "q", "query", "E1");
    CommandResult r = run_acquisition_case(query, base_case());
    assert(r.code == "QUERY_FAILED" && r.evidence.at("driver_error") == "E1" && query.stop_call_count() == 1);
    assert(query.queried_devices().at(0) == "DemoDevice,BID#0");

    FakeDaqAdapter configure;
    configure.enqueue_configure_unsupported("CONFIG_UNSUPPORTED", "c", "configure");
    r = run_acquisition_case(configure, base_case());
    assert(r.status == Status::Skip && configure.configure_requests().size() == 1 && configure.stop_call_count() == 1);

    FakeDaqAdapter trigger;
    trigger.enqueue_trigger_failure("TRIGGER_FAILED", "t", "trigger", "E2");
    TriggerRequest tr;
    tr.source = "external";
    AdapterResult<OperationInfo> tr_result = trigger.configure_trigger(tr);
    assert(!tr_result.success && tr_result.driver_error == "E2" && trigger.trigger_requests().size() == 1);

    FakeDaqAdapter unsupported_meta;
    unsupported_meta.enqueue_query_unsupported("NO_QUERY", "u", "query-stage", "UE");
    r = run_acquisition_case(unsupported_meta, base_case());
    assert(r.evidence.at("stage") == "query-stage" && r.evidence.at("driver_error") == "UE");
}

void test_second_configure_failure_is_first_failure() {
    FakeDaqAdapter a;
    a.enqueue_configure_success();
    a.enqueue_configure_failure("CONFIGURE_SECOND");
    a.enqueue_configure_success();
    a.enqueue_success(data());
    a.enqueue_success(data());
    CommandResult r = run_acquisition_case(a, base_case(3));
    assert(r.code == "CONFIGURE_SECOND" && r.evidence.at("passed_repetitions") == "2");
    assert(r.evidence.at("failure_count") == "1");
    assert(r.evidence.at("failure_0_repetition") == "2");
    assert(r.evidence.at("failure_0_stage") == "configure");
    assert(r.evidence.at("failure_0_code") == "CONFIGURE_SECOND");
    assert(a.configure_call_count() == 3 && a.acquire_call_count() == 2 && a.stop_call_count() == 3);
}

void test_all_repetition_failures_are_aggregated_in_order() {
    FakeDaqAdapter a;
    a.enqueue_failure("TIMEOUT", "first", "acquire");
    a.enqueue_success(data());
    a.enqueue_failure("CACHE_OVERFLOW", "third", "read_buffer");
    CommandResult r = run_acquisition_case(a, base_case(3));
    assert(r.code == "TIMEOUT");
    assert(r.evidence.at("passed_repetitions") == "1");
    assert(r.evidence.at("failure_count") == "2");
    assert(r.evidence.at("failure_0_repetition") == "1" && r.evidence.at("failure_0_stage") == "acquire");
    assert(r.evidence.at("failure_0_code") == "TIMEOUT" && r.evidence.at("failure_0_message") == "first");
    assert(r.evidence.at("failure_1_repetition") == "3" && r.evidence.at("failure_1_stage") == "read_buffer");
    assert(r.evidence.at("failure_1_code") == "CACHE_OVERFLOW" && r.evidence.at("failure_1_message") == "third");
    assert(a.configure_call_count() == 3 && a.acquire_call_count() == 3 && a.stop_call_count() == 3);
}

void test_failure_fields_preserve_delimiters() {
    FakeDaqAdapter a;
    a.enqueue_failure("CODE,:|", "message,:|", "stage,:|");
    CommandResult r = run_acquisition_case(a, base_case());
    assert(r.code == "CODE,:|");
    assert(r.evidence.at("failure_0_stage") == "stage,:|");
    assert(r.evidence.at("failure_0_code") == "CODE,:|");
    assert(r.evidence.at("failure_0_message") == "message,:|");
}

void test_invalid_driver_data_is_rejected_before_math() {
    AcquisitionData invalid_rate = data();
    invalid_rate.actual_sample_rate_hz = std::numeric_limits<double>::quiet_NaN();
    FakeDaqAdapter rate;
    rate.enqueue_success(invalid_rate);
    CommandResult r = run_acquisition_case(rate, base_case());
    assert(r.code == "INVALID_ACQUISITION_DATA" && r.evidence.at("field") == "actual_sample_rate_hz");
    assert(r.exit_category == ExitCategory::ValidationFailed);
    AcquisitionData infinite_rate = data();
    infinite_rate.actual_sample_rate_hz = std::numeric_limits<double>::infinity();
    FakeDaqAdapter inf;
    inf.enqueue_success(infinite_rate);
    assert(run_acquisition_case(inf, base_case()).evidence.at("field") == "actual_sample_rate_hz");
    AcquisitionData negative_duration = data();
    negative_duration.duration_seconds = -1.0;
    FakeDaqAdapter duration;
    duration.enqueue_success(negative_duration);
    assert(run_acquisition_case(duration, base_case()).evidence.at("field") == "duration_seconds");
    AcquisitionData bad_sample = data();
    bad_sample.channels[0].samples[7] = std::numeric_limits<double>::quiet_NaN();
    FakeDaqAdapter sample;
    sample.enqueue_success(bad_sample);
    r = run_acquisition_case(sample, base_case());
    assert(r.code == "INVALID_ACQUISITION_DATA" && r.evidence.at("field") == "sample");
    assert(r.evidence.at("channel") == "0" && r.evidence.at("sample_index") == "7");
}

class ThrowingAdapter : public DaqAdapter {
  public:
    enum Point { Query, Configure, Acquire };
    explicit ThrowingAdapter(Point point) : point_(point), stops_(0) {}
    AdapterResult<CapabilityInfo> query_capabilities(const std::string&) override {
        if (point_ == Query)
            throw std::runtime_error("query");
        AdapterResult<CapabilityInfo> r;
        r.success = true;
        r.value = capability();
        return r;
    }
    AdapterResult<OperationInfo> configure(const AcquisitionRequest&) override {
        if (point_ == Configure)
            throw std::runtime_error("configure");
        AdapterResult<OperationInfo> r;
        r.success = true;
        return r;
    }
    AdapterResult<AcquisitionData> acquire_once(const AcquisitionRequest&) override {
        if (point_ == Acquire)
            throw std::runtime_error("acquire");
        AdapterResult<AcquisitionData> r;
        r.success = true;
        r.value = data();
        return r;
    }
    AdapterResult<OperationInfo> configure_trigger(const TriggerRequest&) override {
        return AdapterResult<OperationInfo>();
    }
    void stop() noexcept override { ++stops_; }
    int stops() const { return stops_; }

  private:
    Point point_;
    int stops_;
};

class UnknownThrowingAdapter : public ThrowingAdapter {
  public:
    UnknownThrowingAdapter() : ThrowingAdapter(Acquire) {}
    AdapterResult<AcquisitionData> acquire_once(const AcquisitionRequest&) override { throw 7; }
};

void test_driver_exceptions_are_caught_and_stopped() {
    ThrowingAdapter::Point points[] = {ThrowingAdapter::Query, ThrowingAdapter::Configure, ThrowingAdapter::Acquire};
    for (int i = 0; i < 3; ++i) {
        ThrowingAdapter a(points[i]);
        CommandResult r = run_acquisition_case(a, base_case(i == 0 ? 1 : 3));
        assert(r.code == "DRIVER_EXCEPTION" && r.exit_category == ExitCategory::Driver &&
               a.stops() == (i == 0 ? 1 : 3));
        const char* stage = i == 0 ? "query_capabilities" : (i == 1 ? "configure" : "acquire");
        assert(r.evidence.at("stage") == stage);
        if (i > 0) {
            assert(r.evidence.at("failure_count") == "3");
            assert(r.evidence.at("failure_0_stage") == stage && r.evidence.at("failure_0_message") == stage);
            assert(r.evidence.at("failure_1_repetition") == "2" && r.evidence.at("failure_2_repetition") == "3");
            assert(r.evidence.at("failed_repetition") == "1");
        }
    }
    UnknownThrowingAdapter unknown;
    CommandResult r = run_acquisition_case(unknown, base_case());
    assert(r.code == "DRIVER_EXCEPTION" && r.message == "Unknown driver exception");
    assert(r.evidence.at("stage") == "acquire" && unknown.stops() == 1);
    assert(r.evidence.at("failure_0_message") == "Unknown driver exception");
}
} // namespace

void test_acquisition() {
    AcquisitionData separated = data();
    separated.trigger_wait_seconds = 10;
    separated.wall_elapsed_seconds = 11;
    AcquisitionRequest separated_request;
    separated_request.channels.push_back(0);
    separated_request.sample_rate_hz = 100;
    separated_request.points_per_channel = 100;
    assert(validate_acquisition_data(separated, separated_request).status == Status::Pass);
    test_three_successes_and_cleanup();
    test_first_failure_keeps_code_and_passed_count();
    test_point_and_channel_counts();
    test_duration_tolerance_boundary();
    test_driver_errors_and_unsupported_are_mapped();
    test_acquisition_event_flags_are_driver_failures();
    test_event_flags_take_priority_over_invalid_payload();
    test_readback_and_low_rate_validation();
    test_invalid_matrix_does_not_touch_hardware();
    test_capabilities_are_enforced_before_configure();
    test_fake_stage_queues_and_request_records();
    test_second_configure_failure_is_first_failure();
    test_all_repetition_failures_are_aggregated_in_order();
    test_failure_fields_preserve_delimiters();
    test_invalid_driver_data_is_rejected_before_math();
    test_driver_exceptions_are_caught_and_stopped();
}
