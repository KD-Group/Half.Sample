#include <iostream>

void test_sampling_config();
void test_phase_schedule();
void test_reconstruction();

int main() {
    test_sampling_config();
    test_phase_schedule();
    test_reconstruction();
    std::cout << "sample_instant_ai_unit_tests: PASS\n";
    return 0;
}
