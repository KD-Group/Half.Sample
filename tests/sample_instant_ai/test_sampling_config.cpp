#include "../../src/config/sampling_config.hpp"
#include "../../src/constant.hpp"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <limits>

void test_sampling_config() {
    Config::SamplingConfig config;
    assert(config.waveform_processing_mode == "threshold_accumulation");
    assert(config.update(2, 0.1, 0.0, 100));
    assert(config.acquisition_mode == Config::AcquisitionMode::Buffered);
    assert(config.sampling_frequency == Constant::MinSamplingFrequency);
    assert(config.waveform_processing_mode == "threshold_accumulation");

    assert(config.update(32, 50.0, 0.0, 100, 10.0, "independent_cycle"));
    assert(config.waveform_processing_mode == "independent_cycle");
    assert(!config.update(32, 50.0, 0.0, 100, 10.0, "unknown_mode"));
    assert(config.waveform_processing_mode == "independent_cycle");

    assert(config.update(1, 50.0, 0.0, 100, 10.0, "independent_cycle"));
    assert(config.sampling_length_per_sample == config.waveform_length * 2);

    // A buffered independent acquisition must fit N+1 complete periods in
    // every RunOnce block; edge confirmation is an internal scan window.
    assert(!config.update(1, 0.1, 0.0, 100, 10.0,
                          "independent_cycle"));

    assert(config.update(64, 50.0, 0.0, 100, 10.0,
                         "independent_cycle"));
    assert(config.waveforms_per_sample == 39);
    assert(config.sampling_time == 2);
    assert(!config.update(1, Constant::MaxSamplingFrequency * 2.0,
                          0.0, 100, 10.0, "independent_cycle"));
    assert(!config.update(INT_MAX, 50.0, 0.0, 100, 10.0,
                          "independent_cycle"));
    assert(!config.update(INT_MAX, 50.0));

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

    assert(!config.update(1, 0.05, 0.1, 100, 0.0));
    assert(!config.update(1, 0.05, 0.1, 100, -1.0));
    assert(!config.update(1, 0.05, 0.1, 100, std::numeric_limits<double>::quiet_NaN()));
    assert(!config.update(1, 0.05, 0.1, 100, std::numeric_limits<double>::infinity()));

    assert(config.update(3, 0.05, 0.1, 100, 10.0));
    const Config::SamplingConfig valid_config = config;
    const auto assert_previous_config_preserved = [&]() {
        assert(config.acquisition_mode == valid_config.acquisition_mode);
        assert(config.number_of_waveforms == valid_config.number_of_waveforms);
        assert(config.emitting_frequency == valid_config.emitting_frequency);
        assert(config.instant_ai_frequency_threshold == valid_config.instant_ai_frequency_threshold);
        assert(config.instant_ai_target_points_per_waveform == valid_config.instant_ai_target_points_per_waveform);
        assert(config.instant_ai_max_reliable_polling_hz == valid_config.instant_ai_max_reliable_polling_hz);
        assert(config.instant_ai_polling_frequency == valid_config.instant_ai_polling_frequency);
        assert(config.instant_ai_planned_duration_seconds == valid_config.instant_ai_planned_duration_seconds);
        assert(config.instant_ai_planned_readings == valid_config.instant_ai_planned_readings);
        assert(config.sampling_frequency == valid_config.sampling_frequency);
        assert(config.sampling_interval == valid_config.sampling_interval);
        assert(config.sampling_length_per_sample == valid_config.sampling_length_per_sample);
        assert(config.waveform_length == valid_config.waveform_length);
        assert(config.valid_length == valid_config.valid_length);
        assert(config.sampling_time == valid_config.sampling_time);
    };

    assert(!config.update(1, std::numeric_limits<double>::denorm_min(), 0.1, 100, 10.0));
    assert_previous_config_preserved();
    assert(!config.update(INT_MAX, 0.05, 0.1, 100, 10.0));
    assert_previous_config_preserved();
    assert(!config.update(160000, 0.1, 0.1, 100, 10.0));
    assert_previous_config_preserved();

    int largest_reconstruction_waveforms = 0;
    for (int waveforms = 1;; ++waveforms) {
        const std::size_t cycles = static_cast<std::size_t>(waveforms) + 1;
        const std::size_t planned = cycles * 100 + 1;
        if (cycles > Constant::MaxInstantAiReconstructionCells / planned) {
            break;
        }
        largest_reconstruction_waveforms = waveforms;
    }
    assert(largest_reconstruction_waveforms == 198);
    assert(config.update(largest_reconstruction_waveforms, 0.1, 0.1, 100, 10.0));
    const Config::SamplingConfig reconstruction_boundary_config = config;
    assert(!config.update(largest_reconstruction_waveforms + 1, 0.1, 0.1, 100, 10.0));
    assert(config.number_of_waveforms == reconstruction_boundary_config.number_of_waveforms);
    assert(config.instant_ai_planned_readings == reconstruction_boundary_config.instant_ai_planned_readings);

    const std::size_t largest_raw_boundary_points =
        (static_cast<std::size_t>(Constant::MaxSamplingPoints) - 1) / 2;
    const std::size_t largest_raw_planned_readings = largest_raw_boundary_points * 2 + 1;
    const std::size_t first_rejected_raw_planned_readings = (largest_raw_boundary_points + 1) * 2 + 1;
    assert(largest_raw_planned_readings <= static_cast<std::size_t>(Constant::MaxSamplingPoints));
    assert(first_rejected_raw_planned_readings > static_cast<std::size_t>(Constant::MaxSamplingPoints));
    assert(!config.update(1, 0.000001, 0.000001, static_cast<int>(largest_raw_boundary_points), 10.0));
    assert(!config.update(1, 0.000001, 0.000001, static_cast<int>(largest_raw_boundary_points + 1), 10.0));
}
