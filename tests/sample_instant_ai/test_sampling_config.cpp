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

    assert(config.update(3, 0.5, 0.5, 100));
    assert(config.acquisition_mode == Config::AcquisitionMode::Instant);
    assert(config.waveform_length == 100);
    assert(config.valid_length == 50);
    assert(std::abs(config.sampling_interval - 20000.0) < 1e-9);

    assert(config.update(3, 0.5001, 0.5, 100));
    assert(config.acquisition_mode == Config::AcquisitionMode::Buffered);
    assert(!config.update(0, 0.1, 0.5, 100));
    assert(!config.update(3, 0.0, 0.5, 100));
    assert(!config.update(3, 0.1, 0.5, 19));
}
