void test_results();
void test_instant_ai_polling();
void test_matrix();
void test_acquisition();
void test_phase_stitcher();
void test_writer();
void test_legacy_adapter();
void test_xnavi_adapter();
void test_suite();

int main() {
    test_results();
    test_instant_ai_polling();
    test_matrix();
    test_acquisition();
    test_phase_stitcher();
    test_writer();
    test_legacy_adapter();
    test_xnavi_adapter();
    test_suite();
    return 0;
}
