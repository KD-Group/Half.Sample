#include "../../src/sampler/instant_ai.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
bool nearly_equal(double left, double right) {
    return std::fabs(left - right) < 1e-10;
}
} // namespace

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

    const auto one_wave = Sampler::InstantAi::build_continuous_schedule(0.05, 1, 100, 10.0);
    assert(nearly_equal(one_wave.duration_seconds, 40.0));
    assert(nearly_equal(one_wave.polling_frequency_hz, 5.0));
    assert(one_wave.planned_seconds.size() == 201);
    assert(one_wave.planned_seconds.front() == 0.0);
    assert(one_wave.planned_seconds.back() == 40.0);

    const auto three_waves = Sampler::InstantAi::build_continuous_schedule(0.01, 3, 100, 10.0);
    assert(nearly_equal(three_waves.duration_seconds, 400.0));
    assert(nearly_equal(three_waves.polling_frequency_hz, 1.0));
    assert(three_waves.planned_seconds.size() == 401);
    const double spacing = three_waves.planned_seconds[1] - three_waves.planned_seconds[0];
    for (std::size_t i = 1; i < three_waves.planned_seconds.size(); ++i) {
        assert(three_waves.planned_seconds[i] > three_waves.planned_seconds[i - 1]);
        if (i + 1 < three_waves.planned_seconds.size()) {
            assert(nearly_equal(three_waves.planned_seconds[i] - three_waves.planned_seconds[i - 1], spacing));
        }
    }
    assert(three_waves.planned_seconds.back() == 400.0);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const double invalid_frequencies[] = {0.0, -1.0, nan, infinity};
    for (double frequency : invalid_frequencies) {
        bool threw = false;
        try {
            Sampler::InstantAi::build_continuous_schedule(frequency, 1, 100, 10.0);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        assert(threw);
    }
    bool threw = false;
    try {
        Sampler::InstantAi::build_continuous_schedule(1.0, 0, 100, 10.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    threw = false;
    try {
        Sampler::InstantAi::build_continuous_schedule(1.0, 1, 19, 10.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    threw = false;
    try {
        Sampler::InstantAi::build_continuous_schedule(1.0, 1, 100, 0.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}
