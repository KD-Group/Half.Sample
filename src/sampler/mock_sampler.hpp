#ifndef MOCK_SAMPLER_HPP
#define MOCK_SAMPLER_HPP

#include "sampler.hpp"

namespace Sampler {

class MockSampler: public Sampler {
    public:
    MockSampler();
    virtual bool sample(const Config::SamplingConfig &config, Result::SamplingResult &result);
    virtual std::string name();

    virtual double get_value(const std::string &key);
    virtual bool set_value(const std::string &key, const double value);

    private:
    double mock_tau;  // us
    double mock_v0;  // V
    double mock_v_inf;  // V
    double mock_noise;  // V
    double mock_is_going_down;  // true(1.0) or false(0.0)
};

} // namespace Sampler

#endif
