#include "../../src/sampler/mock_sampler.hpp"

#include <cassert>
#include <limits>

void test_mock_sampler_controls() {
    Sampler::MockSampler mock;
    assert(mock.set_value("mock_phase_offset", 0.25));
    assert(!mock.set_value("mock_phase_offset", std::numeric_limits<double>::quiet_NaN()));
    assert(!mock.set_value("mock_phase_offset", std::numeric_limits<double>::infinity()));
    assert(mock.get_value("mock_phase_offset") == 0.25);
}
