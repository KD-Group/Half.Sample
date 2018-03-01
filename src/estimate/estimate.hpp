#ifndef ESTIMATE_HPP
#define ESTIMATE_HPP

#include "../types.hpp"

namespace Estimate {

struct EstimatedResult {
    double tau;
    double w, b;
    double loss;
    double interval;
    VectorPtr y;

    EstimatedResult() {};
    EstimatedResult(Waveform wave, double tau);

    void calculate_para();
    void calculate_loss();

    double margin();
};

EstimatedResult one_third_search(Waveform wave);

} // namespace Estimate

#endif
