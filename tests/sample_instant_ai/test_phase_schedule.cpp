#include "../../src/sampler/instant_ai.hpp"

#include <cassert>
#include <cmath>
#include <set>

void test_phase_schedule() {
    const double frequencies[] = {0.1, 0.5, 1.0};
    for (double frequency : frequencies) {
        const auto schedule = Sampler::InstantAi::build_schedule(frequency, 100, 0.1);
        assert(schedule.planned_seconds.size() == 100);
        std::set<int> bins;
        for (std::size_t i = 0; i < schedule.planned_seconds.size(); ++i) {
            bins.insert(Sampler::InstantAi::phase_bin(schedule.planned_seconds[i], frequency, 100));
            if (i) {
                assert(schedule.planned_seconds[i] - schedule.planned_seconds[i - 1] >= 0.1 - 1e-9);
            }
        }
        assert(bins.size() == 100);
        assert(schedule.planned_seconds.back() <= 10.01);
        assert(schedule.deadline_seconds >= schedule.planned_seconds.back() + 0.999);
    }
    const auto slow = Sampler::InstantAi::build_schedule(0.05, 100, 0.1);
    assert(slow.planned_seconds.back() > 19.0);
    assert(slow.planned_seconds.back() < 20.1);
}
