#ifndef SAMPLER_HPP
#define SAMPLER_HPP

#include <memory>
#include <string>
#include "../config/sampling_config.hpp"
#include "../result/sampling_result.hpp"

namespace Sampler {

class Sampler {
  public:
    virtual bool sample(const Config::SamplingConfig& config, Result::SamplingResult& result) = 0;

    virtual std::string name() = 0;

    virtual double get_value(const std::string& key) = 0;

    virtual bool set_value(const std::string& key, double value) = 0;

    static bool dump_origin_data(const Config::SamplingConfig& config, Result::SamplingResult& result);

    static bool load_origin_data(Config::SamplingConfig& config, Result::SamplingResult& result);
};

typedef std::shared_ptr<Sampler> SamplerPtr;

} // namespace Sampler

#endif
