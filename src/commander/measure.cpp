#include "measure.hpp"
#include "base.hpp"
#include "../global/global.hpp"
#include "../processor/processor.hpp"
#include <cstring>
#include <sstream>

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
    return options.eof();
}

} // namespace

namespace Commander {
void measure() {
    bool success = true;

    do {
        success = Global::sampler->sample(Global::config, Global::result);
        if (!success)
            break;

        success = Processor::align(Global::config, Global::result);
        if (!success)
            break;

        success = Processor::summation(Global::config, Global::result);
        if (!success)
            break;

        success = Processor::estimate(Global::config, Global::result);
        if (!success)
            break;
    } while (false);

    Global::result.success = success;
    Global::result.measuring = false;
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

    bool& measuring = Global::result.measuring;
    if (measuring) {
        Base::error(Error::NOW_IN_MEASURING);
        return;
    }

    Config::SamplingConfig requested_config = Global::config;
    requested_config.auto_mode = (mode == "True");
    requested_config.dump_file_path = dump_file_path;
    if (!requested_config.update(number_of_waveforms, emitting_frequency, instant_options.threshold,
                                 instant_options.target_points, instant_options.max_reliable_polling_hz)) {
        Base::error(Error::INVALID_INSTANT_AI_CONFIG);
        return;
    }
    Global::config = requested_config;
    clear_measure_data();

    measuring = true;
    Base::variable(measuring);

#ifdef _WIN32
    DWORD handle;
    CreateThread(NULL, 0, measure, NULL, 0, &handle);
#else
    // start a thread to do measure
    std::thread(measure).detach();
#endif
}

void to_query() {
    bool success = Global::result.success;
    Base::variable(success);
    bool measuring = Global::result.measuring;
    Base::variable(measuring);
    std::string acquisition_mode = Global::config.is_instant() ? "instant_ai" : "buffered_ai";
    Base::variable(acquisition_mode);
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

    if (success) {
        double maximum = Global::result.maximum;
        double minimum = Global::result.minimum;
        Base::variable(maximum);
        Base::variable(minimum);

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
        const double wave_interval = Global::result.estimate.interval;
        Base::variable(wave_interval);
    }

    printf("wave = [");
    if (Global::result.estimate.y) {
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
    bool& measuring = Global::result.measuring;
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

    if (Global::result.measuring) {
        Base::error(Error::NOW_IN_MEASURING);
        return;
    }

    Config::SamplingConfig requested_config = Global::config;
    requested_config.auto_mode = (mode == "True");
    if (!requested_config.update(number_of_waveforms, emitting_frequency, instant_options.threshold,
                                 instant_options.target_points, instant_options.max_reliable_polling_hz)) {
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
    bool measuring = Global::result.measuring;
    Base::variable(planned_seconds);
    Base::variable(elapsed_seconds);
    Base::variable(completed_cycles);
    Base::variable(target_cycles);
    Base::variable(successful_reads);
    Base::variable(late_reads);
    Base::variable(measuring);
}

void to_cancel_sampling() {
    Global::result.progress.cancel_requested.store(true);
}

void to_dump() {
    std::string dump_file_path;
    std::cin >> dump_file_path;
    async_measure(dump_file_path);
}

void to_process() {
    std::string dump_file_path;
    std::cin >> dump_file_path;
    if (Global::result.measuring) {
        Base::error(Error::NOW_IN_MEASURING);
        return;
    }
    clear_measure_data();
    bool& success = Global::result.success;
    success = true;

    Global::config.dump_file_path = dump_file_path;
    success = Sampler::Sampler::load_origin_data(Global::config, Global::result);
    do {
        if (!success)
            break;

        success = Processor::align(Global::config, Global::result);
        if (!success)
            break;

        success = Processor::summation(Global::config, Global::result);
        if (!success)
            break;

        success = Processor::estimate(Global::config, Global::result);
        if (!success)
            break;
    } while (false);

    Global::result.measuring = false;
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
    Global::result.cancelled = false;
    Global::result.progress.reset(Global::config.is_instant()
                                      ? Global::config.instant_ai_planned_duration_seconds
                                      : 0.0,
                                  Global::config.is_instant() ? Global::config.number_of_waveforms + 1 : 0);
    Global::result.error_code = Error::SUCCESS;
    Global::result.maximum = 0.0;
    Global::result.minimum = 0.0;
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
