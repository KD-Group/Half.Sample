#ifndef TYPES_HPP
#define TYPES_HPP

#include <utility>
#include <vector>
#include <memory>

typedef std::vector<double> Vector;
typedef std::shared_ptr<Vector> VectorPtr;

struct Waveform {
    VectorPtr values;
    double interval;
    int rapid_decline_point_idx;

    Waveform(VectorPtr values, double interval, int rapid_decline_point_idx)
        : values(std::move(values)), interval(interval), rapid_decline_point_idx(rapid_decline_point_idx) {

    }
};

#endif
