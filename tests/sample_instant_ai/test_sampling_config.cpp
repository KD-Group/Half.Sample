#include "../../src/config/sampling_config.hpp"
#include "../../src/constant.hpp"

#include <cassert>
#include <cmath>

void test_sampling_config() {
    Config::SamplingConfig config;
    assert(config.update(3, 0.1, 0.0, 100));
    assert(config.acquisition_mode == Config::AcquisitionMode::Buffered);
    assert(config.sampling_frequency == Constant::MinSamplingFrequency);

    assert(config.update(3, 10.0, 0.0, 100));
    assert(config.sampling_frequency == Constant::MaxSamplingFrequency);

    assert(config.update(3, 0.5, 0.5, 100, 50.0));
    assert(config.acquisition_mode == Config::AcquisitionMode::Instant);
    assert(config.waveform_length == 100);
    assert(config.valid_length == 50);
    assert(std::abs(config.sampling_interval - 20000.0) < 1e-9);

    assert(config.update(3, 0.5001, 0.5, 100, 50.0));
    assert(config.acquisition_mode == Config::AcquisitionMode::Buffered);
    assert(!config.update(0, 0.1, 0.5, 100));
    assert(!config.update(3, 0.0, 0.5, 100));
    assert(!config.update(3, 0.1, 0.5, 19, 50.0));

    assert(config.update(3, 0.05, 0.1, 100, 10.0));
    assert(config.acquisition_mode == Config::AcquisitionMode::Instant);
    assert(std::abs(config.instant_ai_planned_duration_seconds - 80.0) < 1e-9);
    assert(std::abs(config.instant_ai_polling_frequency - 5.0) < 1e-9);
    assert(config.instant_ai_planned_readings == 401);

    assert(!config.update(1, 0.05, 0.1, 100, 9.9));
    assert(!config.update(1, 0.05, 0.1, 19, 10.0));
    assert(config.update(1, 0.05, 0.100000000000005, 100, 10.0));

    assert(config.update(1, 0.5, 0.0, 100, 10.0));
    assert(config.acquisition_mode == Config::AcquisitionMode::Buffered);
}
