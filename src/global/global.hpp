#ifndef GLOBAL_HPP
#define GLOBAL_HPP

#include "../sampler/sampler.hpp"
#include "../config/sampling_config.hpp"
#include "../result/sampling_result.hpp"

class Global {
public:
    static Sampler::SamplerPtr sampler;
    static Config::SamplingConfig config;
    static Result::SamplingResult result;
};

#endif
