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

namespace Commander {
void measure() {
    bool& success = Global::result.success;
    success = true;

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

    Global::result.measuring = false;
}

#ifdef _WIN32

DWORD WINAPI measure(void*) {
    measure();
    return 0;
}

#endif

void async_measure() {
    int number_of_waveforms;
    double emitting_frequency;
    std::string mode;
    std::cin >> number_of_waveforms;
    std::cin >> emitting_frequency;
    std::cin >> mode;
    std::string line_tail;
    std::getline(std::cin, line_tail);
    double instant_threshold = 0.0;
    int instant_target_points = 100;
    double instant_max_reliable_polling_hz = 10.0;
    if (!line_tail.empty()) {
        std::istringstream options(line_tail);
        options >> std::ws;
        if (!options.eof()) {
            if (!(options >> instant_threshold)) {
                Base::error(Error::INVALID_INSTANT_AI_CONFIG);
                return;
            }
            options >> std::ws;
            if (!options.eof() && !(options >> instant_target_points)) {
                Base::error(Error::INVALID_INSTANT_AI_CONFIG);
                return;
            }
            options >> std::ws;
            if (!options.eof() && !(options >> instant_max_reliable_polling_hz)) {
                Base::error(Error::INVALID_INSTANT_AI_CONFIG);
                return;
            }
            options >> std::ws;
            if (!options.eof()) {
                Base::error(Error::INVALID_INSTANT_AI_CONFIG);
                return;
            }
        }
    }

    Global::config.auto_mode = (mode == "True");
    if (!Global::config.update(number_of_waveforms, emitting_frequency, instant_threshold, instant_target_points,
                               instant_max_reliable_polling_hz)) {
        Base::error(Error::INVALID_INSTANT_AI_CONFIG);
        return;
    }
    clear_measure_data();

    bool& measuring = Global::result.measuring;
    if (measuring) {
        Base::error(Error::NOW_IN_MEASURING);
        return;
    }

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
    std::string acquisition_mode = Global::config.is_instant() ? "instant_ai" : "buffered_ai";
    Base::variable(acquisition_mode);
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
    Global::config.dump_file_path = "";
    async_measure();
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
    double instant_threshold = 0.0;
    int instant_target_points = 100;
    double instant_max_reliable_polling_hz = 10.0;
    std::istringstream options(line_tail);
    if (!(options >> instant_threshold)) {
        instant_threshold = 0.0;
    } else if (!(options >> instant_target_points)) {
        instant_target_points = 100;
    } else if (!(options >> instant_max_reliable_polling_hz)) {
        instant_max_reliable_polling_hz = 10.0;
    }

    Global::config.auto_mode = (mode == "True");
    if (!Global::config.update(number_of_waveforms, emitting_frequency, instant_threshold, instant_target_points,
                               instant_max_reliable_polling_hz)) {
        Base::error(Error::INVALID_INSTANT_AI_CONFIG);
    }
}

void to_dump() {
    std::cin >> Global::config.dump_file_path;
    async_measure();
}

void to_process() {
    clear_measure_data();
    bool& success = Global::result.success;
    success = true;

    std::cin >> Global::config.dump_file_path;
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
    Global::result.instant_ai_waveforms.clear();
    Global::result.instant_ai_late_reads = 0;
    Global::result.instant_ai_interpolated_bins = 0;
    if (Global::config.is_instant()) {
        Global::result.totalSamplingBuffer.resize(Global::config.sampling_length_per_sample);
        Global::result.resultWave.resize(Global::config.valid_length);
    } else {
        Global::result.totalSamplingBuffer.resize(Global::config.sampling_length_per_sample *
                                                  Global::config.sampling_time);
        Global::result.resultWave.resize(Global::config.valid_length);
    }
    std::memset(Global::result.totalSamplingBuffer.data(), 0,
                Global::result.totalSamplingBuffer.size() * sizeof(double));
    std::memset(Global::result.resultWave.data(), 0, Global::result.resultWave.size() * sizeof(double));
}

} // namespace Commander
