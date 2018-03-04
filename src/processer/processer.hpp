#ifndef PROCESSER_HPP
#define PROCESSER_HPP

#include "../config/sampling_config.hpp"
#include "../result/sampling_result.hpp"

namespace Commander {
namespace Processer {

bool align(const Config::SamplingConfig &config, Result::SamplingResult &result);
bool summation(const Config::SamplingConfig &config, Result::SamplingResult &result);
bool estimate(const Config::SamplingConfig &config, Result::SamplingResult &result);

} // namespace Processer
} // namespace Commander

#endif
