#ifndef PROCESSOR_HPP
#define PROCESSOR_HPP

#include "../config/sampling_config.hpp"
#include "../result/sampling_result.hpp"

namespace Commander {
    namespace Processor {

        bool align(const Config::SamplingConfig &config, Result::SamplingResult &result);

        bool summation(const Config::SamplingConfig &config, Result::SamplingResult &result);

        bool estimate(const Config::SamplingConfig &config, Result::SamplingResult &result);

    } // namespace Processor
} // namespace Commander

#endif
