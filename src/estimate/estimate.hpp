#ifndef ESTIMATE_HPP
#define ESTIMATE_HPP

#include "../types.hpp"

namespace Estimate {

    struct EstimatedResult {
        double tau{};
        double w{}, b{};
        double loss{};
        double interval{};
        VectorPtr y;

        EstimatedResult() = default;

        EstimatedResult(const Waveform& wave, double tau);

        void calculate_para();

        void calculate_loss();

        double margin();
    };

    EstimatedResult one_third_search(const Waveform& wave);

    EstimatedResult eigen_fit_exponential(const Waveform& wave);

    bool is_wave_going_down(const Waveform& wave);

} // namespace Estimate

#endif
