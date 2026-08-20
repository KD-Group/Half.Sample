#include <iostream>

void test_sampling_config();
void test_phase_schedule();
void test_reconstruction();
void test_mock_sampler_controls();
void test_sampling_progress();
void test_controller_owner();
void test_instant_acquisition();
void test_dump_format();
void test_processor_transaction();
void test_phase2_emergency_stop_50hz_average32_supports_32_cycles();
void test_phase2_emergency_stop_50hz_average32_doubled_supports_64_cycles();
void test_independent_cycle_rejects_short_threshold_candidate();
void test_align_keeps_voltage_when_amplitude_is_below_threshold();
void test_independent_cycle_rejects_long_period();
void test_independent_cycle_normal_samples_have_identifiable_tau();
void test_independent_cycle_accumulates_valid_cycles_across_batches();
void test_independent_cycle_uses_edge_platform_means_for_voltage();
void test_independent_cycle_guard_is_one_percent_for_short_periods();
void test_independent_cycle_summation_keeps_acquisition_batches_independent();
void test_independent_cycle_platform_windows_do_not_cross_batch_start();

int main() {
    test_sampling_config();
    test_phase_schedule();
    test_reconstruction();
    test_mock_sampler_controls();
    test_sampling_progress();
    test_controller_owner();
    test_processor_transaction();
    test_instant_acquisition();
    test_dump_format();
    test_independent_cycle_rejects_short_threshold_candidate();
    test_align_keeps_voltage_when_amplitude_is_below_threshold();
    test_independent_cycle_rejects_long_period();
    test_independent_cycle_normal_samples_have_identifiable_tau();
    test_independent_cycle_accumulates_valid_cycles_across_batches();
    test_independent_cycle_uses_edge_platform_means_for_voltage();
    test_independent_cycle_guard_is_one_percent_for_short_periods();
    test_independent_cycle_summation_keeps_acquisition_batches_independent();
    test_independent_cycle_platform_windows_do_not_cross_batch_start();
    test_phase2_emergency_stop_50hz_average32_supports_32_cycles();
    test_phase2_emergency_stop_50hz_average32_doubled_supports_64_cycles();
    std::cout << "sample_instant_ai_unit_tests: PASS\n";
    return 0;
}
