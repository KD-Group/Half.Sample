#include "measure.hpp"
#include "base.hpp"
#include "../global/global.hpp"
#include "../processor/processor.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>

#ifdef _WIN32

#include "Windows.h"

#else
#include <thread>
#include <chrono>
#endif

namespace {

struct InstantOptions {
    double threshold = 0.0;
    int target_points = 100;
    double max_reliable_polling_hz = 10.0;
    std::string waveform_processing_mode = "threshold_accumulation";
};

bool parse_instant_options(const std::string& line_tail, InstantOptions& instant_options) {
    std::istringstream options(line_tail);
    options >> std::ws;
    if (options.eof()) {
        return true;
    }
    if (!(options >> instant_options.threshold)) {
        return false;
    }
    options >> std::ws;
    if (options.eof()) {
        return true;
    }
    if (!(options >> instant_options.target_points)) {
        return false;
    }
    options >> std::ws;
    if (options.eof()) {
        return true;
    }
    if (!(options >> instant_options.max_reliable_polling_hz)) {
        return false;
    }
    options >> std::ws;
    if (options.eof()) {
        return true;
    }
    if (!(options >> instant_options.waveform_processing_mode)) {
        return false;
    }
    options >> std::ws;
    return options.eof();
}

void publish_process_result(Result::SamplingResult& destination, Result::SamplingResult& source) {
    destination.totalSamplingBuffer = std::move(source.totalSamplingBuffer);
    destination.resultWave = std::move(source.resultWave);
    destination.instant_ai_waveforms = std::move(source.instant_ai_waveforms);
    destination.instant_ai_readings = std::move(source.instant_ai_readings);
    destination.instant_ai_format_version = source.instant_ai_format_version;
    destination.instant_ai_complete_waveforms = source.instant_ai_complete_waveforms;
    destination.instant_ai_actual_duration_seconds = source.instant_ai_actual_duration_seconds;
    destination.instant_ai_late_reads = source.instant_ai_late_reads;
    destination.instant_ai_interpolated_bins = source.instant_ai_interpolated_bins;
    destination.requested_waveforms = source.requested_waveforms;
    destination.complete_waveforms = source.complete_waveforms;
    destination.discarded_waveforms = source.discarded_waveforms;
    destination.independent_batches = source.independent_batches;
    destination.independent_retries = source.independent_retries;
    destination.rejected_threshold_candidates = source.rejected_threshold_candidates;
    destination.rejected_short_periods = source.rejected_short_periods;
    destination.rejected_long_periods = source.rejected_long_periods;
    destination.rejected_amplitude_cycles = source.rejected_amplitude_cycles;
    destination.rejected_baseline_cycles = source.rejected_baseline_cycles;
    destination.rejected_shape_cycles = source.rejected_shape_cycles;
    destination.independent_cycle_accumulator = std::move(source.independent_cycle_accumulator);
    destination.independent_cycle_maximum_accumulator =
        std::move(source.independent_cycle_maximum_accumulator);
    destination.independent_cycle_minimum_accumulator =
        std::move(source.independent_cycle_minimum_accumulator);
    destination.cancelled = source.cancelled;
    destination.maximum = source.maximum;
    destination.minimum = source.minimum;
    destination.cycle_maximum = source.cycle_maximum;
    destination.cycle_minimum = source.cycle_minimum;
    destination.v_inf = source.v_inf;
    destination.v_inf_valid = source.v_inf_valid;
    destination.estimate = std::move(source.estimate);
    destination.success = source.success;
    destination.error_code = source.error_code;
    destination.progress.planned_milliseconds.store(source.progress.planned_milliseconds.load());
    destination.progress.elapsed_milliseconds.store(source.progress.elapsed_milliseconds.load());
    destination.progress.completed_cycles.store(source.progress.completed_cycles.load());
    destination.progress.target_cycles.store(source.progress.target_cycles.load());
    destination.progress.successful_reads.store(source.progress.successful_reads.load());
    destination.progress.late_reads.store(source.progress.late_reads.load());
    destination.progress.cancel_requested.store(source.progress.cancel_requested.load());
}

void publish_process_failure(Error::Code error_code,
                             const Result::SamplingResult* partial_result = nullptr) {
    Result::SamplingResult& result = Global::result;
    if (partial_result) {
        result.maximum = partial_result->maximum;
        result.minimum = partial_result->minimum;
        result.cycle_maximum = partial_result->cycle_maximum;
        result.cycle_minimum = partial_result->cycle_minimum;
        result.v_inf = partial_result->v_inf;
        result.v_inf_valid = partial_result->v_inf_valid;
    }
    result.cancelled = false;
    result.success = false;
    result.error_code = error_code;
}

} // namespace

namespace Commander {
void measure() {
    bool success = false;
    const bool independent_mode = Global::config.waveform_processing_mode == "independent_cycle" &&
                                  !Global::config.is_instant();
    int attempts = 0;

    while (true) {
        ++attempts;
        success = Global::sampler->sample(Global::config, Global::result);
        if (success) success = Processor::align(Global::config, Global::result);
        if (success) success = Processor::summation(Global::config, Global::result);
        if (success) success = Processor::estimate(Global::config, Global::result);
        if (success) {
            Global::result.success = true;
            success = Processor::validate_finite_result(Global::config, Global::result);
        }
        if (success || !independent_mode || attempts >= 3 ||
            Global::result.error_code != Error::INSTANT_AI_WAVEFORM_COUNT_INSUFFICIENT)
            break;
    }

    if (independent_mode) Global::result.independent_retries = attempts > 1 ? attempts - 1 : 0;

    Global::result.success = success;
    Global::result.measuring.store(false, std::memory_order_release);
}

#ifdef _WIN32

DWORD WINAPI measure(void*) {
    measure();
    return 0;
}

#endif

void async_measure(const std::string& dump_file_path) {
    int number_of_waveforms;
    double emitting_frequency;
    std::string mode;
    std::cin >> number_of_waveforms;
    std::cin >> emitting_frequency;
    std::cin >> mode;
    std::string line_tail;
    std::getline(std::cin, line_tail);
    InstantOptions instant_options;
    if (!parse_instant_options(line_tail, instant_options)) {
        Base::error(Error::INVALID_INSTANT_AI_CONFIG);
        return;
    }

    Config::SamplingConfig requested_config = Global::config;
    requested_config.auto_mode = (mode == "True");
    requested_config.dump_file_path = dump_file_path;
    if (!requested_config.update(number_of_waveforms, emitting_frequency, instant_options.threshold,
                                 instant_options.target_points, instant_options.max_reliable_polling_hz,
                                 instant_options.waveform_processing_mode)) {
        Base::error(Error::INVALID_INSTANT_AI_CONFIG);
        return;
    }
    bool expected = false;
    if (!Global::result.measuring.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        Base::error(Error::NOW_IN_MEASURING);
        return;
    }
    Global::config = requested_config;
    clear_measure_data();

    bool measuring = true;
    Base::variable(measuring);

#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, measure, NULL, 0, NULL);
    if (!thread) {
        Global::result.error_code = Error::ErrorHandleNotValid;
        Global::result.measuring.store(false, std::memory_order_release);
        Base::error(Error::ErrorHandleNotValid);
        return;
    }
    CloseHandle(thread);
#else
    // start a thread to do measure
    try {
        std::thread(measure).detach();
    } catch (...) {
        Global::result.error_code = Error::ErrorHandleNotValid;
        Global::result.measuring.store(false, std::memory_order_release);
        Base::error(Error::ErrorHandleNotValid);
    }
#endif
}

void to_query() {
    bool measuring = Global::result.measuring.load(std::memory_order_acquire);
    bool success = measuring ? false : Global::result.success;
    Base::variable(success);
    Base::variable(measuring);
    std::string acquisition_mode = Global::config.is_instant() ? "instant_ai" : "buffered_ai";
    Base::variable(acquisition_mode);
    std::string waveform_processing_mode = Global::config.waveform_processing_mode;
    Base::variable(waveform_processing_mode);
    if (measuring) {
        return;
    }
    std::string error_category = Error::category(Global::result.error_code);
    bool retryable = Error::retryable(Global::result.error_code);
    bool cancelled = Global::result.cancelled || Global::result.error_code == Error::USER_CANCELLED;
    int instant_ai_complete_waveforms = Global::result.instant_ai_complete_waveforms;
    double instant_ai_planned_duration_seconds = Global::config.instant_ai_planned_duration_seconds;
    double instant_ai_actual_duration_seconds = Global::result.instant_ai_actual_duration_seconds;
    Base::variable(error_category);
    Base::variable(retryable);
    Base::variable(cancelled);
    Base::variable(instant_ai_complete_waveforms);
    Base::variable(instant_ai_planned_duration_seconds);
    Base::variable(instant_ai_actual_duration_seconds);
    int instant_ai_late_reads = Global::result.instant_ai_late_reads;
    int instant_ai_interpolated_bins = Global::result.instant_ai_interpolated_bins;
    Base::variable(instant_ai_late_reads);
    Base::variable(instant_ai_interpolated_bins);
    int complete_waveforms = Global::result.complete_waveforms;
    int discarded_waveforms = Global::result.discarded_waveforms;
    int independent_batches = Global::result.independent_batches;
    int independent_retries = Global::result.independent_retries;
    int rejected_threshold_candidates = Global::result.rejected_threshold_candidates;
    int rejected_short_periods = Global::result.rejected_short_periods;
    int rejected_long_periods = Global::result.rejected_long_periods;
    int rejected_amplitude_cycles = Global::result.rejected_amplitude_cycles;
    int rejected_baseline_cycles = Global::result.rejected_baseline_cycles;
    int rejected_shape_cycles = Global::result.rejected_shape_cycles;
    Base::variable(complete_waveforms);
    Base::variable(discarded_waveforms);
    Base::variable(independent_batches);
    Base::variable(independent_retries);
    Base::variable(rejected_threshold_candidates);
    Base::variable(rejected_short_periods);
    Base::variable(rejected_long_periods);
    Base::variable(rejected_amplitude_cycles);
    Base::variable(rejected_baseline_cycles);
    Base::variable(rejected_shape_cycles);

    double v_inf = Global::result.v_inf;
    Base::variable(v_inf);
    bool v_inf_valid = Global::result.v_inf_valid;
    Base::variable(v_inf_valid);

    if (success) {
        double maximum = Global::result.maximum;
        double minimum = Global::result.minimum;
        double cycle_maximum = Global::result.cycle_maximum;
        double cycle_minimum = Global::result.cycle_minimum;
        Base::variable(maximum);
        Base::variable(minimum);
        Base::variable(cycle_maximum);
        Base::variable(cycle_minimum);

        const double sampling_interval = Global::config.sampling_interval;
        Base::variable(sampling_interval);
        const double wave_interval = Global::result.estimate.interval;
        Base::variable(wave_interval);

        const double tau = Global::result.estimate.tau;
        Base::variable(tau);
        const double w = Global::result.estimate.w;
        Base::variable(w);
        const double b = Global::result.estimate.b;
        Base::variable(b);
        const double loss = Global::result.estimate.loss;
        Base::variable(loss);

    } else {
        std::string message = Error::to_string(Global::result.error_code);
        Base::variable(message);
        const double wave_interval = 0.0;
        Base::variable(wave_interval);
    }

    printf("wave = [");
    if (success && Global::result.estimate.y) {
        const auto& values = *Global::result.estimate.y;
        for (double value : values) {
            printf("%.3f,", value);
        }
    }
    printf("]\n");
}

void to_measure() {
    async_measure("");
}

void is_measuring() {
    bool measuring = Global::result.measuring.load(std::memory_order_acquire);
    Base::variable(measuring);
}

void to_config() {
    int number_of_waveforms;
    double emitting_frequency;
    std::string mode;
    std::cin >> number_of_waveforms;
    std::cin >> emitting_frequency;
    std::cin >> mode;
    std::string line_tail;
    std::getline(std::cin, line_tail);
    InstantOptions instant_options;
    if (!parse_instant_options(line_tail, instant_options)) {
        Base::error(Error::INVALID_INSTANT_AI_CONFIG);
        return;
    }

    if (Global::result.measuring.load(std::memory_order_acquire)) {
        Base::error(Error::NOW_IN_MEASURING);
        return;
    }

    Config::SamplingConfig requested_config = Global::config;
    requested_config.auto_mode = (mode == "True");
    if (!requested_config.update(number_of_waveforms, emitting_frequency, instant_options.threshold,
                                 instant_options.target_points, instant_options.max_reliable_polling_hz,
                                 instant_options.waveform_processing_mode)) {
        Base::error(Error::INVALID_INSTANT_AI_CONFIG);
        return;
    }
    Global::config = requested_config;
}

void to_sampling_progress() {
    const long long planned_ms = Global::result.progress.planned_milliseconds.load();
    const long long elapsed_ms = Global::result.progress.elapsed_milliseconds.load();
    double planned_seconds = planned_ms / 1000.0;
    double elapsed_seconds = elapsed_ms / 1000.0;
    int completed_cycles = Global::result.progress.completed_cycles.load();
    int target_cycles = Global::result.progress.target_cycles.load();
    int successful_reads = Global::result.progress.successful_reads.load();
    int late_reads = Global::result.progress.late_reads.load();
    bool measuring = Global::result.measuring.load(std::memory_order_acquire);
    Base::variable(planned_seconds);
    Base::variable(elapsed_seconds);
    Base::variable(completed_cycles);
    Base::variable(target_cycles);
    Base::variable(successful_reads);
    Base::variable(late_reads);
    Base::variable(measuring);
}

void to_cancel_sampling() {
    Global::result.progress.request_cancel();
}

void to_dump() {
    std::string dump_file_path;
    std::cin >> dump_file_path;
    async_measure(dump_file_path);
}

void to_process() {
    std::string dump_file_path;
    std::cin >> dump_file_path;
    std::string processing_mode;
    std::getline(std::cin, processing_mode);
    std::istringstream mode_options(processing_mode);
    processing_mode = "threshold_accumulation";
    std::string requested_mode;
    if (mode_options >> requested_mode) {
        processing_mode = requested_mode;
    }
    if (processing_mode != "threshold_accumulation" && processing_mode != "independent_cycle") {
        Base::error(Error::INVALID_INSTANT_AI_CONFIG);
        return;
    }
    if (Global::result.measuring.load(std::memory_order_acquire)) {
        Base::error(Error::NOW_IN_MEASURING);
        return;
    }
    Config::SamplingConfig pending_config = Global::config;
    pending_config.dump_file_path = dump_file_path;
    pending_config.waveform_processing_mode = processing_mode;
    Result::SamplingResult pending_result(false);
    bool success = Sampler::Sampler::load_origin_data(pending_config, pending_result);
    do {
        if (!success)
            break;

        success = Processor::align(pending_config, pending_result);
        if (!success)
            break;

        success = Processor::summation(pending_config, pending_result);
        if (!success)
            break;

        success = Processor::estimate(pending_config, pending_result);
        if (!success)
            break;

        pending_result.success = true;
        success = Processor::validate_finite_result(pending_config, pending_result);
        if (!success)
            break;

    } while (false);

    pending_result.success = success;
    if (success) {
        Global::config = pending_config;
        publish_process_result(Global::result, pending_result);
    } else {
        publish_process_failure(pending_result.error_code, &pending_result);
    }
}

void clear_measure_data() {
    Global::result.success = false;
    Global::result.instant_ai_waveforms.clear();
    Global::result.instant_ai_readings.clear();
    Global::result.instant_ai_format_version = 0;
    Global::result.instant_ai_complete_waveforms = 0;
    Global::result.instant_ai_actual_duration_seconds = 0.0;
    Global::result.instant_ai_late_reads = 0;
    Global::result.instant_ai_interpolated_bins = 0;
    Global::result.requested_waveforms = 0;
    Global::result.complete_waveforms = 0;
    Global::result.discarded_waveforms = 0;
    Global::result.independent_batches = 0;
    Global::result.independent_retries = 0;
    Global::result.rejected_threshold_candidates = 0;
    Global::result.rejected_short_periods = 0;
    Global::result.rejected_long_periods = 0;
    Global::result.rejected_amplitude_cycles = 0;
    Global::result.rejected_baseline_cycles = 0;
    Global::result.rejected_shape_cycles = 0;
    Global::result.independent_cycle_accumulator.clear();
    Global::result.independent_cycle_maximum_accumulator.clear();
    Global::result.independent_cycle_minimum_accumulator.clear();
    Global::result.cancelled = false;
    Global::result.progress.reset(Global::config.is_instant()
                                      ? Global::config.instant_ai_planned_duration_seconds
                                      : 0.0,
                                  Global::config.is_instant() ? Global::config.number_of_waveforms + 1 : 0);
    Global::result.error_code = Error::SUCCESS;
    Global::result.maximum = 0.0;
    Global::result.minimum = 0.0;
    Global::result.cycle_maximum = 0.0;
    Global::result.cycle_minimum = 0.0;
    Global::result.v_inf = 0.0;
    Global::result.v_inf_valid = false;
    Global::result.estimate = Estimate::EstimatedResult();
    if (Global::config.is_instant()) {
        Global::result.totalSamplingBuffer =
            std::vector<double>(static_cast<std::size_t>(Global::config.sampling_length_per_sample), 0.0);
        Global::result.resultWave = std::vector<double>(static_cast<std::size_t>(Global::config.valid_length), 0.0);
    } else {
        Global::result.totalSamplingBuffer = std::vector<double>(
            static_cast<std::size_t>(Global::config.sampling_length_per_sample) *
                static_cast<std::size_t>(Global::config.sampling_time),
            0.0);
        Global::result.resultWave = std::vector<double>(static_cast<std::size_t>(Global::config.valid_length), 0.0);
    }
}

} // namespace Commander
