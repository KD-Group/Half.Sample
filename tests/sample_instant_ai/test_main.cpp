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
    test_phase2_emergency_stop_50hz_average32_supports_32_cycles();
    test_phase2_emergency_stop_50hz_average32_doubled_supports_64_cycles();
    std::cout << "sample_instant_ai_unit_tests: PASS\n";
    return 0;
}
