#include "measure.hpp"
#include "base.hpp"
#include "../global/global.hpp"
#include "../processor/processor.hpp"
#include <cstring>

#ifdef _WIN32

#include "Windows.h"

#else
#include <thread>
#include <chrono>
#endif

namespace Commander {
    void measure() {
        bool &success = Global::result.success;
        success = true;

        do {
            success = Global::sampler->sample(Global::config, Global::result);
            if (!success) break;

            success = Processor::align(Global::config, Global::result);
            if (!success) break;

            success = Processor::summation(Global::config, Global::result);
            if (!success) break;

            success = Processor::estimate(Global::config, Global::result);
            if (!success) break;
        } while (false);

        Global::result.measuring = false;
    }

#ifdef _WIN32

    DWORD WINAPI measure(void *) {
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

        Global::config.auto_mode = (mode == "True");
        Global::config.update(number_of_waveforms, emitting_frequency);
        clear_measure_data();

        bool &measuring = Global::result.measuring;
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
            const auto &values = *Global::result.estimate.y;
            for (double value: values) {
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
        bool &measuring = Global::result.measuring;
        Base::variable(measuring);
    }

    void to_config() {
        int number_of_waveforms;
        double emitting_frequency;
        std::string mode;
        std::cin >> number_of_waveforms;
        std::cin >> emitting_frequency;
        std::cin >> mode;

        Global::config.auto_mode = (mode == "True");
        Global::config.update(number_of_waveforms, emitting_frequency);
    }

    void to_dump() {
        std::cin >> Global::config.dump_file_path;
        async_measure();
    }

    void to_process() {
        clear_measure_data();
        bool &success = Global::result.success;
        success = true;

        std::cin >> Global::config.dump_file_path;
        Sampler::Sampler::load_origin_data(Global::config, Global::result);
        do {
            if (!success) break;

            success = Processor::align(Global::config, Global::result);
            if (!success) break;

            success = Processor::summation(Global::config, Global::result);
            if (!success) break;

            success = Processor::estimate(Global::config, Global::result);
            if (!success) break;
        } while (false);

        Global::result.measuring = false;
    }

    void clear_measure_data() {
        Global::result.totalSamplingBuffer.resize(Global::config.sampling_length_per_sample * Global::config.sampling_time);
        Global::result.resultWave.resize(Global::config.valid_length);
        std::memset(Global::result.totalSamplingBuffer.data(), 0,
                    Global::result.totalSamplingBuffer.size() * sizeof(double));
        std::memset(Global::result.resultWave.data(), 0,
                    Global::result.resultWave.size() * sizeof(double));
    }

} // namespace Commander
