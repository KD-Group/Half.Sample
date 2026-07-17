#include "../../src/sampler/instant_ai.hpp"

#include <cassert>
#include <cmath>

namespace {
Sampler::InstantAi::TimedWaveform make_wave(double offset, int missing_start = -1, int missing_count = 0) {
    Sampler::InstantAi::TimedWaveform readings;
    for (int i = 0; i < 100; ++i) {
        if (i >= missing_start && i < missing_start + missing_count) {
            continue;
        }
        const double phase = (i + 0.5) / 100.0;
        double voltage = offset;
        if (phase < 0.5) {
            voltage += 1.0 + 2.0 * std::exp(-phase * 5.0);
        }
        readings.push_back({phase * 10.0, phase * 10.0, voltage});
    }
    return readings;
}
} // namespace

void test_reconstruction() {
    std::vector<Sampler::InstantAi::TimedWaveform> waves;
    waves.push_back(make_wave(-1.0));
    waves.push_back(make_wave(0.0));
    waves.push_back(make_wave(1.0));
    const auto result = Sampler::InstantAi::reconstruct(waves, 3, 0.1, 100);
    assert(result.success);
    assert(result.averaged_half_wave.size() == 50);

    waves[0] = make_wave(-1.0, 20, 2);
    assert(Sampler::InstantAi::reconstruct(waves, 3, 0.1, 100).success);
    waves[0] = make_wave(-1.0, 0, 2);
    assert(Sampler::InstantAi::reconstruct(waves, 3, 0.1, 100).success);
    waves[0] = make_wave(-1.0, 20, 3);
    assert(!Sampler::InstantAi::reconstruct(waves, 3, 0.1, 100).success);
    waves.resize(2);
    assert(!Sampler::InstantAi::reconstruct(waves, 3, 0.1, 100).success);
}
