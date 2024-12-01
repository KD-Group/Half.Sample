#ifndef SAMPLER_FACTORY_HPP
#define SAMPLER_FACTORY_HPP

#include <iostream>
#include <string>
#include "sampler.hpp"

namespace Sampler {

    class SamplerFactory {
    public:
        static SamplerPtr get(const std::string &sampler_name);
    };

} // namespace Sampler

#endif
