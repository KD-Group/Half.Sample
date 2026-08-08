#include <iostream>

void test_sampling_config();
void test_phase_schedule();
void test_reconstruction();
void test_mock_sampler_controls();
void test_sampling_progress();

int main() {
    test_sampling_config();
    test_phase_schedule();
    test_reconstruction();
    test_mock_sampler_controls();
    test_sampling_progress();
    std::cout << "sample_instant_ai_unit_tests: PASS\n";
    return 0;
}
