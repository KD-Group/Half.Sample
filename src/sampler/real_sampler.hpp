#ifndef REAL_SAMPLER_HPP
#define REAL_SAMPLER_HPP

#include "sampler.hpp"
#ifdef _WIN32
#include "../daq_headers/legacy/bdaqctrl.h"
#endif

namespace Sampler {

class RealSampler : public Sampler {
  public:
    bool sample(const Config::SamplingConfig& config, Result::SamplingResult& result) override;
    double get_value(const std::string& key) override;
    bool set_value(const std::string& key, double value) override;
    std::string name() override;

  private:
    bool sample_buffered(const Config::SamplingConfig& config, Result::SamplingResult& result);
    bool sample_instant(const Config::SamplingConfig& config, Result::SamplingResult& result);
};

} // namespace Sampler
#endif
