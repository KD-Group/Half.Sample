#include "../../src/sampler/instant_ai.hpp"

#include <cassert>
#include <cmath>
#include <limits>

namespace {
const int kPoints = 100;

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

double periodic_voltage(double phase) {
    double local = phase - 0.2;
    if (local < 0.0) {
        local += 1.0;
    }
    return local < 0.5 ? 1.0 + 2.0 * std::exp(-4.0 * local) : 0.0;
}

Sampler::InstantAi::TimedReadings make_continuous(double start_phase, int requested_waveforms,
                                                  int missing_start = -1, int missing_count = 0) {
    Sampler::InstantAi::TimedReadings readings;
    const int samples = (requested_waveforms + 1) * kPoints;
    for (int i = 0; i <= samples; ++i) {
        const double time = i / static_cast<double>(kPoints);
        double phase = std::fmod(time + start_phase, 1.0);
        if (phase < 0.0) {
            phase += 1.0;
        }
        double local = phase - 0.2;
        if (local < 0.0) {
            local += 1.0;
        }
        const int bin = std::min(kPoints - 1, static_cast<int>(local * kPoints + 1e-8));
        if (bin >= missing_start && bin < missing_start + missing_count) {
            continue;
        }
        readings.push_back(Sampler::InstantAi::TimedReading(time, time, periodic_voltage(phase)));
    }
    return readings;
}

Sampler::InstantAi::TimedReadings make_edges(const double* edges, int edge_count) {
    Sampler::InstantAi::TimedReadings readings;
    readings.push_back(Sampler::InstantAi::TimedReading(edges[0] - 0.01, edges[0] - 0.01, 0.0));
    for (int edge = 0; edge < edge_count; ++edge) {
        const double end = edge + 1 < edge_count ? edges[edge + 1] : edges[edge] + 0.7;
        const double step = (end - edges[edge]) / 100.0;
        for (int i = 0; i < 100; ++i) {
            const double time = edges[edge] + i * step;
            readings.push_back(Sampler::InstantAi::TimedReading(time, time, i < 50 ? 1.5 : 0.0));
        }
    }
    return readings;
}
} // namespace

void test_reconstruction() {
    std::vector<Sampler::InstantAi::TimedWaveform> waves;
    waves.push_back(make_wave(-1.0));
    waves.push_back(make_wave(0.0));
    waves.push_back(make_wave(1.0));
    const auto result = Sampler::InstantAi::reconstruct_legacy_waveforms(waves, 3, 0.1, 100);
    assert(result.success);
    assert(result.averaged_half_wave.size() == 50);

    waves[0] = make_wave(-1.0, 20, 2);
    assert(Sampler::InstantAi::reconstruct_legacy_waveforms(waves, 3, 0.1, 100).success);
    waves[0] = make_wave(-1.0, 0, 2);
    assert(Sampler::InstantAi::reconstruct_legacy_waveforms(waves, 3, 0.1, 100).success);
    waves[0] = make_wave(-1.0, 20, 3);
    assert(!Sampler::InstantAi::reconstruct_legacy_waveforms(waves, 3, 0.1, 100).success);
    waves.resize(2);
    assert(!Sampler::InstantAi::reconstruct_legacy_waveforms(waves, 3, 0.1, 100).success);

    const double start_phases[] = {0.0, 0.13, 0.49, 0.91};
    const int waveform_counts[] = {1, 3};
    for (int requested : waveform_counts) {
        for (double phase : start_phases) {
            const auto continuous = Sampler::InstantAi::reconstruct_continuous(
                make_continuous(phase, requested), requested, 1.0, kPoints);
            assert(continuous.status == Sampler::InstantAi::ReconstructionStatus::Success);
            assert(continuous.complete_waveforms == requested);
            assert(continuous.averaged_half_wave.size() == kPoints / 2);
        }
    }

    auto duplicated = make_continuous(0.0, 1);
    duplicated.push_back(Sampler::InstantAi::TimedReading(0.31, 0.31, 3.0));
    duplicated.push_back(Sampler::InstantAi::TimedReading(0.31, 0.31, 5.0));
    const auto averaged = Sampler::InstantAi::reconstruct_continuous(duplicated, 1, 1.0, kPoints);
    assert(averaged.status == Sampler::InstantAi::ReconstructionStatus::Success);
    const double original = periodic_voltage(0.31);
    assert(std::fabs(averaged.averaged_half_wave[10] - (original + 3.0 + 5.0) / 3.0) < 1e-8);

    for (int missing = 1; missing <= 2; ++missing) {
        const auto interpolated = Sampler::InstantAi::reconstruct_continuous(
            make_continuous(0.13, 1, 20, missing), 1, 1.0, kPoints);
        assert(interpolated.status == Sampler::InstantAi::ReconstructionStatus::Success);
        assert(interpolated.interpolated_bins == missing);
    }
    const auto circularly_interpolated = Sampler::InstantAi::reconstruct_continuous(
        make_continuous(0.13, 1, 0, 2), 1, 1.0, kPoints);
    assert(circularly_interpolated.status == Sampler::InstantAi::ReconstructionStatus::Success);
    assert(circularly_interpolated.interpolated_bins == 2);
    const auto uncovered = Sampler::InstantAi::reconstruct_continuous(
        make_continuous(0.13, 1, 20, 3), 1, 1.0, kPoints);
    assert(uncovered.status == Sampler::InstantAi::ReconstructionStatus::CoverageInsufficient);

    auto too_short = make_continuous(0.0, 1);
    too_short.resize(80);
    assert(Sampler::InstantAi::reconstruct_continuous(too_short, 1, 1.0, kPoints).status ==
           Sampler::InstantAi::ReconstructionStatus::WaveformCountInsufficient);

    Sampler::InstantAi::TimedReadings flat;
    for (int i = 0; i < 300; ++i) {
        flat.push_back(Sampler::InstantAi::TimedReading(i / 100.0, i / 100.0, 0.05));
    }
    assert(Sampler::InstantAi::reconstruct_continuous(flat, 1, 1.0, kPoints).status ==
           Sampler::InstantAi::ReconstructionStatus::AlignmentFailed);

    Sampler::InstantAi::TimedReadings descending;
    for (int i = 0; i < 300; ++i) {
        const double time = i / 100.0;
        descending.push_back(Sampler::InstantAi::TimedReading(time, time, 3.0 - time));
    }
    assert(Sampler::InstantAi::reconstruct_continuous(descending, 1, 1.0, kPoints).status ==
           Sampler::InstantAi::ReconstructionStatus::AlignmentFailed);

    const double incompatible_edges[] = {0.2, 1.2, 2.7, 3.7};
    assert(Sampler::InstantAi::reconstruct_continuous(make_edges(incompatible_edges, 4), 2, 1.0, kPoints).status ==
           Sampler::InstantAi::ReconstructionStatus::WaveformCountInsufficient);

    auto failed_rows = make_continuous(0.0, 1);
    failed_rows.push_back(Sampler::InstantAi::TimedReading(1.31, 1.50, 1000.0, false, 5));
    failed_rows.push_back(Sampler::InstantAi::TimedReading(
        1.32, 1.32, std::numeric_limits<double>::quiet_NaN()));
    const auto ignored_failures = Sampler::InstantAi::reconstruct_continuous(failed_rows, 1, 1.0, kPoints);
    assert(ignored_failures.status == Sampler::InstantAi::ReconstructionStatus::Success);
    assert(ignored_failures.late_reads == 1);

    auto invalid_planned_times = make_continuous(0.0, 1);
    for (auto& reading : invalid_planned_times) {
        reading.planned_seconds = std::numeric_limits<double>::quiet_NaN();
    }
    assert(Sampler::InstantAi::reconstruct_continuous(invalid_planned_times, 1, 1.0, kPoints).status ==
           Sampler::InstantAi::ReconstructionStatus::AlignmentFailed);
}
